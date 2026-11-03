# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# SPDX-License-Identifier: Apache-2.0
param([Parameter(Mandatory=$true)][string]$XLang3)
$ErrorActionPreference='Stop'
$parent=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
$fixture=Join-Path $parent ('xlang3-unicode-package-'+[Guid]::NewGuid().ToString('N'))
$library=Join-Path $fixture ([string][char]0x96ea)
$oldLibrary=[Environment]::GetEnvironmentVariable('XLANG3_PYTHON_LIB','Process')
$oldPath=[Environment]::GetEnvironmentVariable('PYTHONPATH','Process')
$passed=$false
try {
    $package=Join-Path $library 'unicode_package_probe'
    [IO.Directory]::CreateDirectory($package)|Out-Null
    [IO.File]::WriteAllText((Join-Path $package '__init__.py'),"VALUE = 19`n")
    [IO.File]::WriteAllText((Join-Path $package 'child.py'),"VALUE = 73`n")
    $env:XLANG3_PYTHON_LIB=$library
    $env:PYTHONPATH=$library
    foreach($source in @('import unicode_package_probe.child as child; assert child.VALUE == 73; print(73)', 'import unicode_package_probe as p; p.__path__ = p.__path__[0]; import unicode_package_probe.child as child; assert child.VALUE == 73; print(73)')) {
        $actual=(& $XLang3 -B -S -c $source|Out-String).Trim()
        if($LASTEXITCODE -ne 0 -or $actual -ne '73') {throw "Unicode nested package failed (exit $LASTEXITCODE): $actual"}
    }
    $passed=$true
    Write-Output 'Unicode nested package imports with list and string paths passed.'
} finally {
    [Environment]::SetEnvironmentVariable('XLANG3_PYTHON_LIB',$oldLibrary,'Process')
    [Environment]::SetEnvironmentVariable('PYTHONPATH',$oldPath,'Process')
    if($passed) {
        if([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($fixture)) -ne $parent) {throw 'Unsafe fixture cleanup target'}
        Remove-Item -LiteralPath $fixture -Recurse -Force
    } else {Write-Host "Failed Unicode package fixture retained: $fixture"}
}
