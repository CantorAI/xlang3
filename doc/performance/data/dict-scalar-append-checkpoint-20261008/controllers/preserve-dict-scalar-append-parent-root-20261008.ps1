$ErrorActionPreference = 'Stop'
$taskRepo = 'D:\CantorAI\xlang3'
Set-Location -LiteralPath $taskRepo
$taskProofPath = 'scratch/performance/dict-scalar-append-runtime-index-proposed-20261008-provenance.json'
$taskProof = Get-Content -LiteralPath $taskProofPath -Raw | ConvertFrom-Json
$taskR4Path = 'build-repro/controls/frame-locals-retirement-r4-validated-checkpoint-20261008/preserved-release-provenance.json'
$taskR4 = Get-Content -LiteralPath $taskR4Path -Raw | ConvertFrom-Json
$taskDestination = Join-Path $taskRepo 'build-repro/controls/dict-scalar-append-runtime-index-parent-20261008'
if (Test-Path -LiteralPath $taskDestination) { throw 'Preservation destination already exists; inspect it, do not overwrite.' }
function Get-TaskHash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Assert-TaskMap([string]$root, $map) {
    foreach ($entry in $map.PSObject.Properties) {
        if ((Get-TaskHash (Join-Path $root $entry.Name)) -ne $entry.Value) { throw "Input hash mismatch: $($entry.Name)" }
    }
}
if ((git rev-parse HEAD) -ne $taskProof.head) { throw 'HEAD changed' }
if (git diff --cached --name-only) { throw 'Index is not empty' }
Assert-TaskMap $taskRepo $taskProof.raw_before_sha256
$taskRelease = Join-Path $taskRepo 'build-repro/main-verify-20261006/Release'
$taskBaseline = Join-Path $taskRepo 'build-repro/Release'
Assert-TaskMap $taskRelease $taskR4.files_sha256
Assert-TaskMap $taskBaseline $taskR4.fixed_baseline_sha256
if (@(Get-ChildItem -LiteralPath $taskRelease -Recurse -File).Count -ne 178) { throw 'Unexpected candidate Release file count' }
if (@(Get-ChildItem -LiteralPath $taskBaseline -Recurse -File).Count -ne 177) { throw 'Unexpected fixed baseline file count' }
New-Item -ItemType Directory -Path $taskDestination | Out-Null
Copy-Item -LiteralPath $taskRelease -Destination (Join-Path $taskDestination 'Release') -Recurse
foreach ($entry in $taskProof.raw_before_sha256.PSObject.Properties) {
    $target = Join-Path (Join-Path $taskDestination 'source-snapshot') $entry.Name
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath (Join-Path $taskRepo $entry.Name) -Destination $target
}
Assert-TaskMap (Join-Path $taskDestination 'Release') $taskR4.files_sha256
Assert-TaskMap (Join-Path $taskDestination 'source-snapshot') $taskProof.raw_before_sha256
Assert-TaskMap $taskRepo $taskProof.raw_before_sha256
Assert-TaskMap $taskRelease $taskR4.files_sha256
Assert-TaskMap $taskBaseline $taskR4.fixed_baseline_sha256
$taskManifest = [ordered]@{
    status = 'preserved_validated_r4_scalar_append_trial_parent'; terminal = $true; full_validated = $true
    head = $taskProof.head; source_count = 114; file_count = 178
    inventory_scope = '111 registered R4 sources plus three critical native inputs; not all compiled repository sources'
    files_sha256 = $taskR4.files_sha256; source_snapshot_sha256 = $taskProof.raw_before_sha256
    fixed_baseline_sha256 = $taskR4.fixed_baseline_sha256
    full_validation_sha256 = $taskR4.full_validation_sha256
    parent_preservation_sha256 = Get-TaskHash $taskR4Path
    proposal_proof_sha256 = Get-TaskHash $taskProofPath
    decision_sha256 = Get-TaskHash 'scratch/performance/dict-scalar-append-freshness-trial-decision-root-20261008.json'
    preserved_utc = [DateTime]::UtcNow.ToString('o')
}
$taskManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskDestination 'preserved-release-provenance.json') -Encoding utf8NoBOM
[ordered]@{ status = $taskManifest.status; source_count = 114; release_file_count = 178; manifest_sha256 = Get-TaskHash (Join-Path $taskDestination 'preserved-release-provenance.json') } | ConvertTo-Json
