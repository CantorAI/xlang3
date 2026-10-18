# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# SPDX-License-Identifier: Apache-2.0
param([Parameter(Mandatory=$true)][string]$XLang3)
$ErrorActionPreference='Stop'
$fixtureParent=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
$fixtureRoot=Join-Path $fixtureParent ('xlang3-unicode-library-'+[Guid]::NewGuid().ToString('N'))
$library=Join-Path $fixtureRoot ([string][char]0x96ea)
$previousLibrary=[Environment]::GetEnvironmentVariable('XLANG3_PYTHON_LIB','Process')
$passed=$false
try {
    [IO.Directory]::CreateDirectory($library) | Out-Null
    [IO.File]::WriteAllText((Join-Path $library 'xlang3_unicode_library_probe.py'),"VALUE = 'native-wide-library-root'`n",[Text.UTF8Encoding]::new($false))
    $env:XLANG3_PYTHON_LIB=$library
    # Isolate native import-root discovery; product site startup is verified
    # separately by xMind with a complete staged pure-source library.
    $output=& $XLang3 -B -S -c "import xlang3_unicode_library_probe as probe; assert probe.VALUE == 'native-wide-library-root'; print('UNICODE_LIBRARY_ROOT_OK')" 2>&1
    $code=$LASTEXITCODE
    if($code -ne 0 -or ($output | Out-String).Trim() -ne 'UNICODE_LIBRARY_ROOT_OK') {
        throw "Unicode native library root failed (exit $code): $($output | Out-String)"
    }
    $passed=$true
    Write-Output 'Native Unicode library environment path passed'
} finally {
    [Environment]::SetEnvironmentVariable('XLANG3_PYTHON_LIB',$previousLibrary,'Process')
    $resolved=[IO.Path]::GetFullPath($fixtureRoot)
    if([IO.Path]::GetDirectoryName($resolved) -ne $fixtureParent -or -not [IO.Path]::GetFileName($resolved).StartsWith('xlang3-unicode-library-')) {
        throw 'Unsafe Unicode library fixture cleanup target'
    }
    if($passed) { Remove-Item -LiteralPath $resolved -Recurse -Force }
    else { Write-Output "Failed native Unicode library fixture retained at $resolved" }
}
