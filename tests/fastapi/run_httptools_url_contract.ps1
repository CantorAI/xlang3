param(
    [Parameter(Mandatory = $true)][string]$XLang3,
    [string]$CPython = 'C:\Python\Python314\python.exe',
    [string]$XLangPackages,
    [string]$CPythonPackages
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $XLangPackages) {
    $XLangPackages = Join-Path $root 'scratch\httptools-url-probe'
}
if (-not $CPythonPackages) {
    $CPythonPackages = Join-Path $root 'scratch\httptools-cpython'
}
$source = Join-Path $PSScriptRoot 'httptools_url_contract.py'
$expected = ((Get-Content (Join-Path $PSScriptRoot 'httptools_url_contract.out') -Raw) -replace "`r`n", "`n").TrimEnd()
$previousPythonPath = $env:PYTHONPATH
try {
    $env:PYTHONPATH = $CPythonPackages
    $oracle = ((& $CPython $source | Out-String) -replace "`r`n", "`n").TrimEnd()
    if ($LASTEXITCODE -ne 0 -or $oracle -ne $expected) {
        throw 'CPython httptools URL oracle differs from the checked-in expected output'
    }
    $env:PYTHONPATH = $XLangPackages
    $actual = ((& $XLang3 $source | Out-String) -replace "`r`n", "`n").TrimEnd()
    if ($LASTEXITCODE -ne 0 -or $actual -ne $oracle) {
        throw "XLang3 httptools URL output differs from CPython:`n$actual"
    }
    Write-Output 'httptools URL contract matches CPython 3.14'
}
finally {
    $env:PYTHONPATH = $previousPythonPath
}
