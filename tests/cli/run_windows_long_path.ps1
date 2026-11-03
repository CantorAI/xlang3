# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# SPDX-License-Identifier: Apache-2.0
param([Parameter(Mandatory=$true)][string]$XLang3)
$ErrorActionPreference='Stop'
$fixtureParent=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
$fixtureRoot=Join-Path $fixtureParent ('xlang3-long-path-'+[Guid]::NewGuid().ToString('N'))
$library=$fixtureRoot
while($library.Length -lt 280){$library=Join-Path $library (('p'*45)+[char]0x96ea)}
$module=Join-Path $library 'xlang3_long_path_probe.py'
$output=Join-Path $library 'roundtrip.txt'
$previousLibrary=[Environment]::GetEnvironmentVariable('XLANG3_PYTHON_LIB','Process')
$previousPythonPath=[Environment]::GetEnvironmentVariable('PYTHONPATH','Process')
$passed=$false
function Extended([string]$value){if($value.StartsWith('\\?\')){return $value};if($value.StartsWith('\\')){return '\\?\UNC\'+$value.Substring(2)};return '\\?\'+$value}
try {
    [IO.Directory]::CreateDirectory((Extended $library)) | Out-Null
    [IO.File]::WriteAllText((Extended $module),"VALUE = 'native-wide-long-path'`n",[Text.UTF8Encoding]::new($false))
    $env:XLANG3_PYTHON_LIB=$library
    $env:PYTHONPATH=$library
    $absolute=($output.Replace('\','/') | ConvertTo-Json -Compress)
    $relative=($output.Substring($fixtureRoot.Length+1).Replace('\','/') | ConvertTo-Json -Compress)
    $probe=@"
import xlang3_long_path_probe as probe
assert probe.VALUE == 'native-wide-long-path'
source = open(probe.__file__, 'r', encoding='utf-8')
assert 'native-wide-long-path' in source.read()
source.close()
target = open($absolute, 'w', encoding='utf-8')
target.write('absolute-long-path')
target.close()
target = open($relative, 'r', encoding='utf-8')
assert target.read() == 'absolute-long-path'
target.close()
target = open($relative, 'w', encoding='utf-8')
target.write('relative-long-path')
target.close()
target = open($absolute, 'r', encoding='utf-8')
assert target.read() == 'relative-long-path'
target.close()
print('LONG_PATH_IMPORT_AND_IO_OK')
"@
    $probePath=Join-Path $fixtureRoot 'probe.py'
    [IO.File]::WriteAllText($probePath,$probe,[Text.UTF8Encoding]::new($false))
    Push-Location -LiteralPath $fixtureRoot
    $previousAction=$ErrorActionPreference
    try{$ErrorActionPreference='Continue';$result=& $XLang3 -B -S $probePath 2>&1;$code=$LASTEXITCODE}finally{$ErrorActionPreference=$previousAction;Pop-Location}
    if($code -ne 0 -or ($result | Out-String).Trim() -ne 'LONG_PATH_IMPORT_AND_IO_OK'){throw "Long-path import/I/O failed (exit $code): $($result | Out-String)"}
    $passed=$true
    Write-Output "Native long-path import and absolute/relative I/O passed ($($module.Length) characters)"
} finally {
    [Environment]::SetEnvironmentVariable('XLANG3_PYTHON_LIB',$previousLibrary,'Process')
    [Environment]::SetEnvironmentVariable('PYTHONPATH',$previousPythonPath,'Process')
    $resolved=[IO.Path]::GetFullPath($fixtureRoot)
    if([IO.Path]::GetDirectoryName($resolved) -ne $fixtureParent -or -not [IO.Path]::GetFileName($resolved).StartsWith('xlang3-long-path-')){throw 'Unsafe long-path fixture cleanup target'}
    if($passed){[IO.Directory]::Delete((Extended $resolved),$true)}else{Write-Output "Failed native long-path fixture retained at $resolved"}
}
