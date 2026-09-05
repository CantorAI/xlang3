param([Parameter(Mandatory=$true)][string]$XLang3)
$ErrorActionPreference = 'Stop'
$port = Get-Random -Minimum 30001 -Maximum 35000
$prefix = Join-Path ([IO.Path]::GetTempPath()) ('xlang3-pressure-exit-' + [Guid]::NewGuid().ToString('N'))
$server = $null
$clients = @()
try {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $XLang3
    $info.Arguments = '"' + (Join-Path $PSScriptRoot 'pressure_exit_server.py') + '" ' + $port + ' "' + $prefix + '"'
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $server = [Diagnostics.Process]::Start($info)
    $serverError = $server.StandardError.ReadToEndAsync()
    $serverOutput = $server.StandardOutput.ReadToEndAsync()
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while (-not (Test-Path -LiteralPath "$prefix.ready")) {
        if ($server.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw 'Pressure-exit server failed to start' }
        Start-Sleep -Milliseconds 50
    }
    foreach ($index in 0..1) {
        $clients += Start-Process -FilePath $XLang3 -ArgumentList @("$PSScriptRoot/pressure_exit_client.py", "$port") `
            -WindowStyle Hidden -PassThru -RedirectStandardOutput "$prefix.$index.out" -RedirectStandardError "$prefix.$index.err"
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while (-not (Test-Path -LiteralPath "$prefix.entered")) {
        if ($server.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw 'Large call did not enter server' }
        Start-Sleep -Milliseconds 50
    }
    Start-Sleep -Milliseconds 500
    foreach ($client in $clients) {
        if ($client.HasExited) { throw 'Capacity wait failed before the server was stopped' }
    }
    Stop-Process -Id $server.Id -Force
    $server.WaitForExit()
    foreach ($index in 0..1) {
        $client = $clients[$index]
        if (-not $client.WaitForExit(10000)) { throw 'Client hung after server exit under capacity pressure' }
        $client.Refresh()
        $errorText = Get-Content "$prefix.$index.err" -Raw
        if ($client.ExitCode -eq 0 -or $errorText -notmatch 'exited') {
            throw "Unexpected pressure-exit result: $errorText"
        }
    }
    Write-Output 'pressure-server-exit-passed'
} finally {
    foreach ($client in $clients) {
        if (-not $client.HasExited) { $client.Kill(); $client.WaitForExit() }
        $client.Dispose()
    }
    if ($server) {
        if (-not $server.HasExited) { $server.Kill(); $server.WaitForExit() }
        $server.Dispose()
    }
}
