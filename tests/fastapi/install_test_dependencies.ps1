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
