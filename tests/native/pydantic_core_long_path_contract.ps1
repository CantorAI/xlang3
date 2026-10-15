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
    [Parameter(Mandatory = $true)][string]$XLang3,
    [Parameter(Mandatory = $true)][string]$Modules,
    [Parameter(Mandatory = $true)][string]$OutputRoot
)

$ErrorActionPreference = "Stop"

function Get-ExtendedPath([string]$Path) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    if ($fullPath.StartsWith('\\')) {
        return '\\?\UNC\' + $fullPath.TrimStart('\')
    }
    return '\\?\' + $fullPath
}

$segment = 'longseg'
$fixtureRoot = [IO.Path]::GetFullPath($OutputRoot)
$targetModules = Join-Path $fixtureRoot 'native-modules'
$targetDll = Join-Path $targetModules 'pydantic_core._pydantic_core.x3pkg.dll'
while ($targetDll.Length -lt 262) {
    $targetModules = Join-Path $targetModules $segment
    $targetDll = Join-Path $targetModules 'pydantic_core._pydantic_core.x3pkg.dll'
}
$pythonSite = Join-Path $fixtureRoot 'python-site'
$targetPackage = Join-Path $pythonSite 'pydantic_core'
$sourceDll = Join-Path $Modules 'pydantic_core._pydantic_core.x3pkg.dll'
$sourceSchemaGather = Join-Path (Join-Path $Modules 'pydantic_core') '_schema_gather.py'

[void][IO.Directory]::CreateDirectory($targetPackage)
[void][IO.Directory]::CreateDirectory((Get-ExtendedPath $targetModules))
[IO.File]::Copy((Get-ExtendedPath $sourceDll), (Get-ExtendedPath $targetDll), $true)
[IO.File]::Copy($sourceSchemaGather, (Join-Path $targetPackage '_schema_gather.py'), $true)
[IO.File]::WriteAllText(
    (Join-Path $targetPackage '__init__.py'),
    'from ._pydantic_core import __version__ as __version__' + "`n",
    [Text.UTF8Encoding]::new($false)
)

if ($targetDll.Length -lt 262 -or $targetDll.Length -ge 280) {
    throw "Native package fixture path must exercise a path beyond MAX_PATH (262-279 characters): $($targetDll.Length)"
}

$previousPythonPath = $env:PYTHONPATH
$stdoutPath = Join-Path $OutputRoot 'pydantic-core-long-path.stdout'
$stderrPath = Join-Path $OutputRoot 'pydantic-core-long-path.stderr'
try {
    $env:PYTHONPATH = "$pythonSite;$targetModules"
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = [IO.Path]::GetFullPath($XLang3)
    $startInfo.Arguments = '-B -c "import pydantic_core;print(pydantic_core.__version__)"'
    $startInfo.WorkingDirectory = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($XLang3))
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.EnvironmentVariables['PYTHONPATH'] = "$pythonSite;$targetModules"

    $process = [Diagnostics.Process]::Start($startInfo)
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    [IO.File]::WriteAllText($stdoutPath, $stdout)
    [IO.File]::WriteAllText($stderrPath, $stderr)
    $output = $stdout.Trim()
    if ($process.ExitCode -ne 0) {
        throw "xlang3 failed to import pydantic_core from a $($targetDll.Length)-character path (exit $($process.ExitCode)): $stderr"
    }
    if (-not $output) {
        throw 'pydantic_core imported without reporting its version'
    }
    Write-Host "long native package path loaded: $output ($($targetDll.Length) characters)"
}
finally {
    $env:PYTHONPATH = $previousPythonPath
}
