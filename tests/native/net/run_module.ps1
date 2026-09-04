param(
  [Parameter(Mandatory = $true)]
  [string]$XLang3
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$serverScript = Join-Path $root "server.py"
$clientScript = Join-Path $root "client.py"
$serverOut = Join-Path $env:TEMP "xlang3_net_server.out"
$serverErr = Join-Path $env:TEMP "xlang3_net_server.err"
Remove-Item -LiteralPath $serverOut, $serverErr -Force -ErrorAction SilentlyContinue

Write-Host "Starting net test server: $serverScript"
$server = Start-Process -FilePath $XLang3 -ArgumentList @($serverScript) -PassThru -WindowStyle Hidden -RedirectStandardOutput $serverOut -RedirectStandardError $serverErr
try {
  $ready = $false
  for ($i = 0; $i -lt 100; ++$i) {
    if ($server.HasExited) {
      throw "Net HTTP server exited before accepting clients."
    }
    try {
      $tcp = [System.Net.Sockets.TcpClient]::new()
      $async = $tcp.BeginConnect("127.0.0.1", 18173, $null, $null)
      if ($async.AsyncWaitHandle.WaitOne(100)) {
        $tcp.EndConnect($async)
        $ready = $true
        $tcp.Close()
        Write-Host "Net test server is listening on 127.0.0.1:18173"
        break
      }
      $tcp.Close()
    } catch {
    }
    Start-Sleep -Milliseconds 50
  }
  if (-not $ready) {
    throw "Net HTTP server did not open port 18173."
  }

  Write-Host "Running net test client: $clientScript"
  $output = & $XLang3 $clientScript
  if ($LASTEXITCODE -ne 0) {
    $output | Write-Host
    if (Test-Path $serverOut) { Get-Content $serverOut | Write-Host }
    if (Test-Path $serverErr) { Get-Content $serverErr | Write-Host }
    throw "Net HTTP client script failed."
  }
  $expected = @("True", "200", "hello", "small", "True", "65536", "True", "4", "True", "True", "200", "hello", "True", "200")
  $actual = @($output)
  if ($actual.Count -ne $expected.Count) {
    $actual | Write-Host
    throw "Unexpected net client output line count: $($actual.Count)"
  }
  Write-Host "Net test client output:"
  $actual | ForEach-Object { Write-Host "  $_" }
  for ($i = 0; $i -lt $expected.Count; ++$i) {
    if ($actual[$i] -ne $expected[$i]) {
      $actual | Write-Host
      throw "Unexpected net client output at line $($i + 1): expected '$($expected[$i])', got '$($actual[$i])'"
    }
  }
  Write-Host "Net server/client test PASS"
} finally {
  if (-not $server.HasExited) {
    Stop-Process -Id $server.Id -Force
  }
}
