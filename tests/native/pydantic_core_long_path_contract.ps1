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

function Get-NormalizedModulePath([string]$Path) {
    if ($Path.StartsWith('\\?\UNC\')) {
        $Path = '\\' + $Path.Substring(8)
    } elseif ($Path.StartsWith('\\?\')) {
        $Path = $Path.Substring(4)
    }
    return [IO.Path]::GetFullPath($Path).Replace('/', '\')
}

# A successful import alone is insufficient: the runtime may silently fall back
# to the short packaged DLL. Keep the child alive until Windows reports which
# native module was actually mapped, then release only this fixture's child.
foreach ($pathStyle in @('native', 'forward-slash')) {
    $searchModules = $targetModules
    if ($pathStyle -eq 'forward-slash') {
        $searchModules = $targetModules.Replace('\', '/')
    }
    $stdoutPath = Join-Path $OutputRoot "pydantic-core-long-path-$pathStyle.stdout"
    $stderrPath = Join-Path $OutputRoot "pydantic-core-long-path-$pathStyle.stderr"
    $originPath = Join-Path $OutputRoot "pydantic-core-long-path-$pathStyle.origin.json"
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = [IO.Path]::GetFullPath($XLang3)
    $startInfo.Arguments = '-B -c "import sys;import pydantic_core;print(pydantic_core.__version__,flush=True);sys.stdin.readline()"'
    $startInfo.WorkingDirectory = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($XLang3))
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.RedirectStandardInput = $true
    $startInfo.EnvironmentVariables['PYTHONPATH'] = "$pythonSite;$searchModules"

    $process = [Diagnostics.Process]::Start($startInfo)
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $output = ''
    try {
        $versionTask = $process.StandardOutput.ReadLineAsync()
        if (-not $versionTask.Wait(10000)) {
            throw "Timed out waiting for pydantic_core import ($pathStyle)"
        }
        $output = $versionTask.Result
        if (-not $output) {
            throw "pydantic_core imported without reporting its version ($pathStyle)"
        }
        $process.Refresh()
        $mappedPaths = @($process.Modules | Where-Object {
            $_.ModuleName -eq 'pydantic_core._pydantic_core.x3pkg.dll'
        } | ForEach-Object { Get-NormalizedModulePath $_.FileName })
        [IO.File]::WriteAllText($originPath, ([ordered]@{
            path_style = $pathStyle
            expected_path = $targetDll
            expected_length = $targetDll.Length
            mapped_paths = $mappedPaths
            version = $output
        } | ConvertTo-Json -Depth 3))
        if ($mappedPaths.Count -ne 1 -or -not [string]::Equals(
            $mappedPaths[0], (Get-NormalizedModulePath $targetDll),
            [StringComparison]::OrdinalIgnoreCase)) {
            throw "Expected the long fixture DLL ($pathStyle), but mapped: $($mappedPaths -join '; ')"
        }
    }
    finally {
        if (-not $process.HasExited) {
            $process.StandardInput.WriteLine('done')
            $process.StandardInput.Close()
            if (-not $process.WaitForExit(5000)) {
                $process.Kill()
                $process.WaitForExit()
            }
        }
        [IO.File]::WriteAllText($stdoutPath, [string]$output)
        [IO.File]::WriteAllText($stderrPath, $stderrTask.GetAwaiter().GetResult())
    }
    if ($process.ExitCode -ne 0) {
        throw "xlang3 failed to import pydantic_core ($pathStyle, exit $($process.ExitCode))"
    }
    Write-Host "long native package path verified: $output ($($targetDll.Length) characters, $pathStyle)"
}
