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
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Program,

    [string]$XLang3 = "D:\CantorAI\xlang3\build\Release\xlang3.exe",

    [string]$Devenv = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\devenv.exe",

    [switch]$NoStopAtEntry,

    [string[]]$Args = @()
)

$ErrorActionPreference = "Stop"

function Convert-ToJsonString([string]$Text) {
    return ($Text | ConvertTo-Json -Compress)
}

if (-not (Test-Path -LiteralPath $XLang3)) {
    throw "XLang3 executable was not found: $XLang3"
}

if (-not (Test-Path -LiteralPath $Program)) {
    throw "Python file was not found: $Program"
}

if (-not (Test-Path -LiteralPath $Devenv)) {
    throw "Visual Studio was not found: $Devenv"
}

$programPath = (Resolve-Path -LiteralPath $Program).Path
$adapterPath = (Resolve-Path -LiteralPath $XLang3).Path
$launchPath = Join-Path $env:TEMP ("xlang3-vs-debug-{0}.json" -f ([Guid]::NewGuid().ToString("N")))
$stopAtEntry = -not $NoStopAtEntry.IsPresent
$jsonArgs = ($Args | ForEach-Object { Convert-ToJsonString $_ }) -join ", "

$launchJson = @"
{
  "`$adapter": $(Convert-ToJsonString $adapterPath),
  "`$adapterArgs": "--dap-stdio",
  "type": "xlang3",
  "request": "launch",
  "program": $(Convert-ToJsonString $programPath),
  "stopAtEntry": $($stopAtEntry.ToString().ToLowerInvariant()),
  "args": [$jsonArgs]
}
"@

Set-Content -LiteralPath $launchPath -Value $launchJson -Encoding UTF8

& $Devenv /Command "DebugAdapterHost.Logging /On /OutputWindow"
& $Devenv /Command "DebugAdapterHost.Launch /LaunchJson:`"$launchPath`""

Write-Host "XLang3 Visual Studio debug launch sent."
Write-Host "Program: $programPath"
Write-Host "Adapter: $adapterPath --dap-stdio"
Write-Host "Launch JSON: $launchPath"
