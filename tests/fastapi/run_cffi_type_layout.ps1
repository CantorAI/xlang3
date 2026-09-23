param(
    [Parameter(Mandatory = $true)][string]$XLang3,
    [Parameter(Mandatory = $true)][string]$Python314,
    [string]$TestPackages = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'scratch\fastapi-test-deps')
)

$ErrorActionPreference = 'Stop'
$previousPythonPath = $env:PYTHONPATH
try {
    $env:PYTHONPATH = (Resolve-Path -LiteralPath $TestPackages).Path
    foreach ($case in @('cffi_type_layout', 'cffi_from_buffer', 'cffi_struct_fields')) {
        $source = Join-Path $PSScriptRoot ($case + '.py')
        $expected = ((Get-Content -LiteralPath (Join-Path $PSScriptRoot ($case + '.out')) -Raw) -replace "`r`n", "`n").TrimEnd()
        $oracle = ((& $Python314 $source | Out-String) -replace "`r`n", "`n").TrimEnd()
        if ($LASTEXITCODE -ne 0 -or $oracle -ne $expected) {
            throw "CPython 3.14 $case oracle failed: '$oracle'"
        }
        $actual = ((& $XLang3 $source | Out-String) -replace "`r`n", "`n").TrimEnd()
        if ($LASTEXITCODE -ne 0 -or $actual -ne $oracle) {
            throw "XLang3 $case differs from CPython 3.14: '$actual'"
        }
    }
    Write-Host 'CFFI layout, native calls, buffers, and struct fields match CPython 3.14'
} finally {
    $env:PYTHONPATH = $previousPythonPath
}
