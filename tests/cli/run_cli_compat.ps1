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
    throw "WorkDir is required"
}

New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

function Assert-Output([string]$Name, [string]$Expected, [scriptblock]$Run) {
    $actual = ((& $Run | Out-String) -replace "`r`n", "`n").TrimEnd()
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
    if ($actual -ne $Expected) {
        throw "$Name output mismatch. Expected '$Expected', got '$actual'"
    }
}

$commandSource = "import sys`nprint(sys.argv)"
Assert-Output "command argv" "['-c', 'alpha', 'beta']" { & $XLang3 -c $commandSource alpha beta }

$scriptPath = Join-Path $WorkDir "script_argv.py"
$scriptPathRepr = $scriptPath -replace "\\", "\\"
Set-Content -LiteralPath $scriptPath -Value "import sys`nprint(sys.argv)" -NoNewline
Assert-Output "script argv" "['$scriptPathRepr', 'one', 'two']" { & $XLang3 $scriptPath one two }

$modulePath = Join-Path $WorkDir "cli_module_probe.py"
Set-Content -LiteralPath $modulePath -Value "import sys`nprint(sys.argv)" -NoNewline
Assert-Output "module argv" "['cli_module_probe', 'red', 'blue']" {
    Push-Location $WorkDir
    try {
        & $XLang3 -m cli_module_probe red blue
    } finally {
        Pop-Location
    }
}

Assert-Output "ignored -X option" "['-c', 'gamma']" {
    & $XLang3 -X frozen_modules=off -c "import sys`nprint(sys.argv)" gamma
}

Assert-Output "ignored compact -X option" "['-c', 'delta']" {
    & $XLang3 -Xdev -c "import sys`nprint(sys.argv)" delta
}

$packageDir = Join-Path $WorkDir "cli_package_dir"
$packageDirRepr = $packageDir -replace "\\", "\\"
New-Item -ItemType Directory -Force -Path $packageDir | Out-Null
Set-Content -LiteralPath (Join-Path $packageDir "__main__.py") -Value "import sys`nprint(sys.argv)" -NoNewline
Assert-Output "directory __main__ argv" "['$packageDirRepr', 'left', 'right']" {
    & $XLang3 $packageDir left right
}

$pathPackageDir = Join-Path $WorkDir "cli_path_package_dir"
New-Item -ItemType Directory -Force -Path $pathPackageDir | Out-Null
Set-Content -LiteralPath (Join-Path $pathPackageDir "__main__.py") -Value "import sys`nsys.path[0] = sys.path[0] + '/../'`nprint(sys.path[0])`ndel sys.path[0]`nprint(len(sys.path) > 0)" -NoNewline
Assert-Output "directory sys.path mutation" "$pathPackageDir/../`nTrue" {
    & $XLang3 $pathPackageDir
}

$pythonExe = Join-Path (Split-Path -Parent $XLang3) "python.exe"
if (-not (Test-Path $pythonExe)) {
    throw "python.exe copy missing next to xlang3.exe"
}

Assert-Output "python exe alias" "['-c', 'alias']" {
    & $pythonExe -c "import sys`nprint(sys.argv)" alias
}
