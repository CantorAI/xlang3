# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# SPDX-License-Identifier: Apache-2.0
param([Parameter(Mandatory=$true)][string]$XLang3)
$ErrorActionPreference='Stop'
$parent=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixture=Join-Path $parent ('xlang3-bytecode-environment-'+[Guid]::NewGuid().ToString('N'))
$saved=@{}
foreach($name in @('PYTHONDONTWRITEBYTECODE','XLANG3_PYTHON_LIB','PYTHONPATH')) {$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
$passed=$false
function Expect([string]$label,[string]$expected,[string[]]$arguments) {
    $actual=(& $XLang3 @arguments | Out-String).Trim()
    if($LASTEXITCODE -ne 0 -or $actual -ne $expected) {throw "$label expected '$expected', got '$actual' (exit $LASTEXITCODE)"}
}
try {
    [IO.Directory]::CreateDirectory($fixture)|Out-Null
    $env:XLANG3_PYTHON_LIB=$fixture
    $env:PYTHONPATH=$fixture
    $flags='import sys; print(sys.flags.dont_write_bytecode, sys.dont_write_bytecode)'
    $env:PYTHONDONTWRITEBYTECODE='1'
    Expect 'inherited bytecode policy' '1 True' @('-S','-c',$flags)
    [IO.File]::WriteAllText((Join-Path $fixture 'no_cache_probe.py'),"VALUE = 73`n")
    Expect 'environment disables actual import cache' '73' @('-S','-c','import no_cache_probe; print(no_cache_probe.VALUE)')
    if(Test-Path -LiteralPath (Join-Path $fixture '__pycache__')) {throw 'Environment policy allowed a bytecode cache'}
    $env:PYTHONDONTWRITEBYTECODE='0'
    Expect 'any nonempty policy value' '1 True' @('-S','-c',$flags)
    Expect 'ignore environment' '0 False' @('-E','-S','-c',$flags)
    Expect 'isolated ignores environment' '0 False' @('-I','-S','-c',$flags)
    Expect 'explicit flag with ignored environment' '1 True' @('-E','-B','-S','-c',$flags)
    [Environment]::SetEnvironmentVariable('PYTHONDONTWRITEBYTECODE',$null,'Process')
    Expect 'unset policy keeps default' '0 False' @('-S','-c',$flags)
    $passed=$true
    Write-Output 'Bytecode environment, actual import and isolation policies passed.'
} finally {
    foreach($name in $saved.Keys) {[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}
    if($passed) {
        if([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($fixture)) -ne $parent.TrimEnd('\')) {throw 'Fixture cleanup escaped temporary parent'}
        Remove-Item -LiteralPath $fixture -Recurse -Force
    } else {Write-Host "Failed bytecode fixture retained: $fixture"}
}
