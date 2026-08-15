param(
    [string]$XLang3 = "../build/Release/xlang3.exe",
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"

Write-Host "Python:"
& $Python --version

Write-Host ""
Write-Host "CPython scalar_loop.py"
Measure-Command { & $Python "$PSScriptRoot/scalar_loop.py" } | Select-Object TotalMilliseconds

Write-Host ""
Write-Host "XLang3 scalar_loop.py"
Measure-Command { & $XLang3 "$PSScriptRoot/scalar_loop.py" } | Select-Object TotalMilliseconds
