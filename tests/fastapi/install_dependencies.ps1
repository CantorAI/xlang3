param(
    [Parameter(Mandatory = $true)][string]$Python314,
    [string]$Destination = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'scratch\fastapi-deps')
)

$ErrorActionPreference = 'Stop'
$requirements = Join-Path $PSScriptRoot 'requirements.txt'
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
& $Python314 -m pip install `
    --disable-pip-version-check `
    --upgrade `
    --target $Destination `
    --requirement $requirements
if ($LASTEXITCODE -ne 0) {
    throw "FastAPI dependency installation failed with exit code $LASTEXITCODE"
}
