# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0
param(
    [string]$XLang3,
    [int]$Port = 0,
    [string]$NativeModules = "",
    [string]$ClientScript = "client.py",
    [string]$Expected = "expected.out",
    [string]$NativeClient = ""
)

$ErrorActionPreference = "Stop"

if (-not $XLang3) {
    throw "XLang3 executable path is required"
}

$root = $PSScriptRoot
$serverSource = Join-Path $root "server.py"
$clientSource = Join-Path $root $ClientScript
$expectedPath = Join-Path $root $Expected

if ($Port -eq 0) {
    $Port = Get-Random -Minimum 19000 -Maximum 24000
}
$serverArguments = @($serverSource, [string]$Port)
if ($NativeModules) { $serverArguments += $NativeModules }

$logPrefix = Join-Path ([System.IO.Path]::GetTempPath()) "xlang3_lrpc_smoke_$PID"
$serverOut = "$logPrefix.out.log"
$serverErr = "$logPrefix.err.log"

Remove-Item -LiteralPath $serverOut, $serverErr -ErrorAction SilentlyContinue

$server = $null
$parallelClients = @()
try {
    $server = Start-Process `
        -FilePath $XLang3 `
        -ArgumentList $serverArguments `
        -RedirectStandardOutput $serverOut `
        -RedirectStandardError $serverErr `
        -WindowStyle Hidden `
        -PassThru

    $null = $server.Handle

    $ready = "lrpc-ready:$Port"
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline) {
        if ($server.HasExited) {
            $stdout = if (Test-Path $serverOut) { Get-Content -LiteralPath $serverOut -Raw } else { "" }
            $stderr = if (Test-Path $serverErr) { Get-Content -LiteralPath $serverErr -Raw } else { "" }
            throw "LRPC server PID $($server.Id) exited before ready on port $Port (exit $($server.ExitCode)). stdout='$stdout' stderr='$stderr'"
        }

        if ((Test-Path $serverOut) -and ((Get-Content -LiteralPath $serverOut -Raw) -like "*$ready*")) {
            break
        }

        Start-Sleep -Milliseconds 100
    }

    if (-not (Test-Path $serverOut) -or ((Get-Content -LiteralPath $serverOut -Raw) -notlike "*$ready*")) {
        $stdout = if (Test-Path $serverOut) { Get-Content -LiteralPath $serverOut -Raw } else { "" }
        $stderr = if (Test-Path $serverErr) { Get-Content -LiteralPath $serverErr -Raw } else { "" }
        throw "LRPC server did not become ready on port $Port. stdout='$stdout' stderr='$stderr'"
    }

    $expected = ((Get-Content -LiteralPath $expectedPath -Raw) -replace "`r`n", "`n").TrimEnd()
    $actual = ((& $XLang3 $clientSource $Port | Out-String) -replace "`r`n", "`n").TrimEnd()
    if ($LASTEXITCODE -ne 0) {
        throw "LRPC client failed with exit code $LASTEXITCODE. Output '$actual'"
    }
    if ($actual -ne $expected) {
        throw "LRPC client output mismatch. Expected '$expected', got '$actual'"
    }

    for ($i = 0; $i -lt 4; $i++) {
        $clientOut = "$logPrefix.client.$i.out.log"
        $clientErr = "$logPrefix.client.$i.err.log"
        Remove-Item -LiteralPath $clientOut, $clientErr -ErrorAction SilentlyContinue
        $parallelClients += [pscustomobject]@{
            Index = $i
            Out = $clientOut
            Err = $clientErr
            Process = Start-Process `
                -FilePath $XLang3 `
                -ArgumentList @($clientSource, [string]$Port) `
                -RedirectStandardOutput $clientOut `
                -RedirectStandardError $clientErr `
                -WindowStyle Hidden `
                -PassThru
        }
        $null = $parallelClients[-1].Process.Handle
    }

    foreach ($client in $parallelClients) {
        if (-not $client.Process.WaitForExit(30000)) {
            Stop-Process -Id $client.Process.Id -Force
            throw "Parallel LRPC client $($client.Index) timed out"
        }
        $client.Process.WaitForExit()
        $client.Process.Refresh()
        $clientActual = if (Test-Path $client.Out) { (([string](Get-Content -LiteralPath $client.Out -Raw)) -replace "`r`n", "`n").TrimEnd() } else { "" }
        $clientError = if (Test-Path $client.Err) { Get-Content -LiteralPath $client.Err -Raw } else { "" }
        $clientExitCode = $client.Process.ExitCode
        if ($null -ne $clientExitCode -and $clientExitCode -ne 0) {
            $serverStatus = if ($server.HasExited) { "exit=$($server.ExitCode)" } else { "running" }
            throw "Parallel LRPC client $($client.Index) failed with exit code $clientExitCode. stdout='$clientActual' stderr='$clientError' server=$serverStatus"
        }
        if ($clientActual -ne $expected) {
            throw "Parallel LRPC client $($client.Index) output mismatch. Expected '$expected', got '$clientActual'. stderr='$clientError'"
        }
    }

    if ($NativeClient) {
        & $NativeClient $Port
        if ($LASTEXITCODE -ne 0) { throw "C++ LRPC client failed with exit code $LASTEXITCODE" }
    }
    Write-Host "ipc lrpc smoke ok"
}
finally {
    foreach ($client in $parallelClients) {
        if (-not $client.Process.HasExited) {
            Stop-Process -Id $client.Process.Id -Force
            $client.Process.WaitForExit()
        }
    }
    if ($server -ne $null -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force
        $server.WaitForExit()
    }
}
