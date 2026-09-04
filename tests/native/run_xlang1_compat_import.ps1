# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
param(
    [string]$XLang3
)

$ErrorActionPreference = "Stop"

if (-not $XLang3) {
    throw "XLang3 executable path is required"
}

$root = $PSScriptRoot
$source = Join-Path $root "xlang1_compat_import.py"
$expectedPath = Join-Path $root "xlang1_compat_import.out"
$expected = ((Get-Content -LiteralPath $expectedPath -Raw) -replace "`r`n", "`n").TrimEnd()
$actual = ((& $XLang3 $source | Out-String) -replace "`r`n", "`n").TrimEnd()
if ($LASTEXITCODE -ne 0) {
    throw "xlang1 compat import failed with exit code $LASTEXITCODE"
}
if ($actual -ne $expected) {
    throw "xlang1 compat import output mismatch. Expected '$expected', got '$actual'"
}
Write-Host "xlang1 compat import fixture ok"
