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
    [string]$XLang3
)

$ErrorActionPreference = "Stop"

if (-not $XLang3) {
    throw "XLang3 executable path is required"
}

$root = $PSScriptRoot
$source = Join-Path $root "real_native_imports.py"
$expectedPath = Join-Path $root "real_native_imports.out"
$expected = ((Get-Content -LiteralPath $expectedPath -Raw) -replace "`r`n", "`n").TrimEnd()
$actual = ((& $XLang3 $source | Out-String) -replace "`r`n", "`n").TrimEnd()
if ($LASTEXITCODE -ne 0) {
    throw "real_native_imports failed with exit code $LASTEXITCODE"
}
if ($actual -ne $expected) {
    throw "real_native_imports output mismatch. Expected '$expected', got '$actual'"
}
Write-Host "real native import fixture ok"
