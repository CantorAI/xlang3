# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
param(
  [Parameter(Mandatory = $true)]
  [string]$XLang3
)

$ErrorActionPreference = "Stop"

function New-DapFrame([hashtable]$Message) {
  $json = $Message | ConvertTo-Json -Compress -Depth 16
  $length = [Text.Encoding]::UTF8.GetByteCount($json)
  return "Content-Length: $length`r`n`r`n$json"
}

function Read-DapFrames([string]$Text) {
  $frames = @()
  $offset = 0
  while ($offset -lt $Text.Length) {
    $headerEnd = $Text.IndexOf("`r`n`r`n", $offset, [StringComparison]::Ordinal)
    if ($headerEnd -lt 0) {
      break
    }
    $header = $Text.Substring($offset, $headerEnd - $offset)
    if ($header -notmatch "Content-Length:\s*(\d+)") {
      throw "missing Content-Length in DAP response"
    }
    $length = [int]$Matches[1]
    $payloadStart = $headerEnd + 4
    $payload = $Text.Substring($payloadStart, $length)
    $frames += ,($payload | ConvertFrom-Json)
    $offset = $payloadStart + $length
  }
  return $frames
}

$inputText = ""
$inputText += New-DapFrame @{
  seq = 1
  type = "request"
  command = "initialize"
  arguments = @{}
}
$inputText += New-DapFrame @{
  seq = 2
  type = "request"
  command = "launch"
  arguments = @{
    program = "dap_stdio_smoke.py"
    source = "print(42)`n"
  }
}
$inputText += New-DapFrame @{
  seq = 3
  type = "request"
  command = "setExceptionBreakpoints"
  arguments = @{
    filters = @()
  }
}
$inputText += New-DapFrame @{
  seq = 4
  type = "request"
  command = "configurationDone"
  arguments = @{}
}
$inputText += New-DapFrame @{
  seq = 5
  type = "request"
  command = "threads"
  arguments = @{}
}
$inputText += New-DapFrame @{
  seq = 6
  type = "request"
  command = "disconnect"
  arguments = @{}
}

$start = New-Object Diagnostics.ProcessStartInfo
$start.FileName = $XLang3
$start.Arguments = "--dap-stdio"
$start.UseShellExecute = $false
$start.RedirectStandardInput = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.StandardOutputEncoding = [Text.Encoding]::UTF8
$start.StandardErrorEncoding = [Text.Encoding]::UTF8

$process = [Diagnostics.Process]::Start($start)
$process.StandardInput.Write($inputText)
$process.StandardInput.Close()
$stdout = $process.StandardOutput.ReadToEnd()
$stderr = $process.StandardError.ReadToEnd()
$process.WaitForExit()

if ($process.ExitCode -ne 0) {
  throw "xlang3 --dap-stdio failed with $($process.ExitCode): $stderr"
}

$frames = Read-DapFrames $stdout
if ($frames.Count -lt 5) {
  throw "expected at least five DAP messages, got $($frames.Count): $stdout"
}

$initialize = $frames | Where-Object { $_.type -eq "response" -and $_.command -eq "initialize" } | Select-Object -First 1
if ($null -eq $initialize -or -not $initialize.success) {
  throw "initialize response missing or unsuccessful"
}

$initialized = $frames | Where-Object { $_.type -eq "event" -and $_.event -eq "initialized" } | Select-Object -First 1
if ($null -eq $initialized) {
  throw "initialized event missing"
}

$launch = $frames | Where-Object { $_.type -eq "response" -and $_.command -eq "launch" } | Select-Object -First 1
if ($null -eq $launch -or -not $launch.success) {
  throw "launch response missing or unsuccessful"
}

$exceptions = $frames | Where-Object { $_.type -eq "response" -and $_.command -eq "setExceptionBreakpoints" } | Select-Object -First 1
if ($null -eq $exceptions -or -not $exceptions.success) {
  throw "setExceptionBreakpoints response missing or unsuccessful"
}

$threads = $frames | Where-Object { $_.type -eq "response" -and $_.command -eq "threads" } | Select-Object -First 1
if ($null -eq $threads -or -not $threads.success -or $threads.body.threads[0].id -ne 1) {
  throw "threads response missing or wrong"
}

$output = $frames | Where-Object { $_.type -eq "event" -and $_.event -eq "output" } | Select-Object -First 1
if ($null -eq $output -or $output.body.output -ne "42`n") {
  throw "output event missing or wrong"
}

$terminated = $frames | Where-Object { $_.type -eq "event" -and $_.event -eq "terminated" } | Select-Object -First 1
if ($null -eq $terminated) {
  throw "terminated event missing"
}
