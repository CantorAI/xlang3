# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0
param(
    [string]$XLang3,
    [int]$Port = 0
)

$ErrorActionPreference = "Stop"

if (-not $XLang3) {
    throw "XLang3 executable path is required"
}

$root = $PSScriptRoot
$serverSource = Join-Path $root "server.py"
$clientSource = Join-Path $root "server_exit_client.py"

if ($Port -eq 0) {
    $Port = Get-Random -Minimum 24001 -Maximum 29000
}

$logPrefix = Join-Path ([System.IO.Path]::GetTempPath()) "xlang3_lrpc_server_exit_$PID"
$serverOut = "$logPrefix.out.log"
$serverErr = "$logPrefix.err.log"

Remove-Item -LiteralPath $serverOut, $serverErr -ErrorAction SilentlyContinue

$server = $null
try {
    $server = Start-Process `
        -FilePath $XLang3 `
        -ArgumentList @($serverSource, [string]$Port) `
        -RedirectStandardOutput $serverOut `
        -RedirectStandardError $serverErr `
        -WindowStyle Hidden `
        -PassThru

    $ready = "lrpc-ready:$Port"
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline) {
        if ($server.HasExited) {
            $stdout = if (Test-Path $serverOut) { Get-Content -LiteralPath $serverOut -Raw } else { "" }
            $stderr = if (Test-Path $serverErr) { Get-Content -LiteralPath $serverErr -Raw } else { "" }
            throw "LRPC server exited before ready. stdout='$stdout' stderr='$stderr'"
        }

        if ((Test-Path $serverOut) -and ((Get-Content -LiteralPath $serverOut -Raw) -like "*$ready*")) {
            break
        }

        Start-Sleep -Milliseconds 100
    }

    if (-not (Test-Path $serverOut) -or ((Get-Content -LiteralPath $serverOut -Raw) -notlike "*$ready*")) {
        throw "LRPC server did not become ready on port $Port"
    }

    Stop-Process -Id $server.Id -Force
    $server.WaitForExit()

    $clientOut = "$logPrefix.client.out.log"
    $clientErr = "$logPrefix.client.err.log"
    Remove-Item -LiteralPath $clientOut, $clientErr -ErrorAction SilentlyContinue
    $client = Start-Process `
        -FilePath $XLang3 `
        -ArgumentList @($clientSource, [string]$Port) `
        -RedirectStandardOutput $clientOut `
        -RedirectStandardError $clientErr `
        -WindowStyle Hidden `
        -PassThru
    if (-not $client.WaitForExit(10000)) {
        Stop-Process -Id $client.Id -Force
        throw "LRPC client timed out after server exit"
    }
    $client.WaitForExit()
    $client.Refresh()
    $actual = if (Test-Path $clientOut) { Get-Content -LiteralPath $clientOut -Raw } else { "" }
    $clientError = if (Test-Path $clientErr) { Get-Content -LiteralPath $clientErr -Raw } else { "" }
    if ($client.ExitCode -eq 0) {
        throw "LRPC client unexpectedly succeeded after server exit. Output '$actual'"
    }
    if ($actual -like "*ipc-smoke*") {
        throw "LRPC client read remote data after server exit. stdout='$actual' stderr='$clientError'"
    }

    Write-Host "ipc lrpc server-exit ok"
}
finally {
    if ($server -ne $null -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force
        $server.WaitForExit()
    }
}
