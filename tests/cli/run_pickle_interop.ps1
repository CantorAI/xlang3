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
    [string]$XLang3,
    [string]$WorkDir
)

$ErrorActionPreference = "Stop"

if (-not $XLang3) {
    throw "XLang3 executable path is required"
}

if (-not $WorkDir) {
    $WorkDir = Join-Path $PSScriptRoot "..\..\build\pickle_interop"
}

$python = "C:\Python\Python314\python.exe"
if (-not (Test-Path $python)) {
    $python = (Get-Command python -ErrorAction SilentlyContinue).Source
}

if (-not $python) {
    Write-Host "pickle interop skipped: CPython not found"
    exit 0
}

$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$writer = Join-Path $root "tests\fixtures\compat_sections\pickle_interop_writer.py"
$reader = Join-Path $root "tests\fixtures\compat_sections\pickle_interop_reader.py"

New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
Push-Location $WorkDir
try {
    Remove-Item -LiteralPath .\xlang3_pickle_interop.bin, .\cpython_pickle_interop.bin -ErrorAction SilentlyContinue

    $writeOutput = & $XLang3 $writer
    if ($LASTEXITCODE -ne 0 -or (($writeOutput -join "`n").Trim()) -ne "pickle-written") {
        throw "XLang3 pickle writer failed: $writeOutput"
    }

    $cpyRead = & $python -c "import pickle; f=open('xlang3_pickle_interop.bin','rb'); data=pickle.load(f); f.close(); print(data['name'], data['items'][2], data['flag'])"
    if ($LASTEXITCODE -ne 0 -or (($cpyRead -join "`n").Trim()) -ne "xlang3 3 True") {
        throw "CPython could not read XLang3 pickle: $cpyRead"
    }

    & $python -c "import pickle; f=open('cpython_pickle_interop.bin','wb'); pickle.dump({'name':'cpython','items':[1,2,3],'flag':False}, f, 4); f.close()"
    if ($LASTEXITCODE -ne 0) {
        throw "CPython pickle writer failed"
    }

    $xlangRead = & $XLang3 $reader
    if ($LASTEXITCODE -ne 0 -or (($xlangRead -join "`n").Trim()) -ne "cpython 3 False") {
        throw "XLang3 could not read CPython pickle: $xlangRead"
    }
} finally {
    Pop-Location
}

Write-Host "pickle interop ok"
