param(
    [Parameter(Mandatory = $true)][string]$XLang3,
    [Parameter(Mandatory = $true)][string]$Python314,
    [string]$TestPackages = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'scratch\fastapi-test-deps')
)

$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'cffi_type_layout.py'
$expected = ((Get-Content -LiteralPath (Join-Path $PSScriptRoot 'cffi_type_layout.out') -Raw) -replace "`r`n", "`n").TrimEnd()
$previousPythonPath = $env:PYTHONPATH
try {
    $env:PYTHONPATH = (Resolve-Path -LiteralPath $TestPackages).Path
    $oracle = ((& $Python314 $source | Out-String) -replace "`r`n", "`n").TrimEnd()
    if ($LASTEXITCODE -ne 0 -or $oracle -ne $expected) {
        throw "CPython 3.14 CFFI layout oracle failed: '$oracle'"
    }
    $actual = ((& $XLang3 $source | Out-String) -replace "`r`n", "`n").TrimEnd()
    if ($LASTEXITCODE -ne 0 -or $actual -ne $oracle) {
        throw "XLang3 CFFI layout differs from CPython 3.14: '$actual'"
    }
    Write-Host 'CFFI layout and native calls match CPython 3.14'
} finally {
    $env:PYTHONPATH = $previousPythonPath
}
