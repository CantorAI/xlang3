param(
    [Parameter(Mandatory = $true)][string]$Python314,
    [string]$Destination = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'scratch\fastapi-test-deps')
)

$ErrorActionPreference = 'Stop'
$requirements = Join-Path $PSScriptRoot 'requirements-test.txt'
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
& $Python314 -m pip install `
    --disable-pip-version-check `
    --upgrade `
    --no-deps `
    --target $Destination `
    --requirement $requirements
if ($LASTEXITCODE -ne 0) {
    throw "FastAPI test dependency installation failed with exit code $LASTEXITCODE"
}

# Uvicorn pins this version in uv.lock. Select the upstream universal wheel so
# its optional CPython speedups are not installed into the XLang3 test target.
$wheelhouse = Join-Path $Destination '_pure_wheels'
New-Item -ItemType Directory -Force -Path $wheelhouse | Out-Null
& $Python314 -m pip download `
    --disable-pip-version-check `
    --no-deps `
    --only-binary=:all: `
    --platform any `
    --implementation py `
    --abi none `
    --dest $wheelhouse `
    websockets==16.1.1
if ($LASTEXITCODE -ne 0) {
    throw "Pure-Python websockets wheel download failed with exit code $LASTEXITCODE"
}
$websocketsWheel = Get-ChildItem -LiteralPath $wheelhouse -Filter 'websockets-16.1.1-py3-none-any.whl' -File | Select-Object -First 1
if (-not $websocketsWheel) {
    throw 'The pure-Python websockets wheel is unavailable'
}
& $Python314 -m pip install `
    --disable-pip-version-check `
    --upgrade `
    --no-deps `
    --no-compile `
    --target $Destination `
    $websocketsWheel.FullName
if ($LASTEXITCODE -ne 0) {
    throw "Pure-Python websockets installation failed with exit code $LASTEXITCODE"
}
