param(
    [Parameter(Mandatory=$true)][string]$Proposal,
    [Parameter(Mandatory=$true)][string]$ProposalSha256
)
$ErrorActionPreference = 'Stop'
$taskRepo = 'D:\CantorAI\xlang3'
Set-Location -LiteralPath $taskRepo
function Get-TaskHash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Assert-TaskMap([string]$root, $map) {
    foreach ($entry in $map.PSObject.Properties) {
        if ((Get-TaskHash (Join-Path $root $entry.Name)) -ne $entry.Value) { throw "Hash mismatch: $($entry.Name)" }
    }
}
if ((Get-TaskHash $Proposal) -ne $ProposalSha256) { throw 'Proposal hash mismatch' }
$taskProof = Get-Content -LiteralPath $Proposal -Raw | ConvertFrom-Json
$taskParentRoot = Join-Path $taskRepo 'build-repro/controls/dict-scalar-append-runtime-index-parent-20261008'
$taskParentPath = Join-Path $taskParentRoot 'preserved-release-provenance.json'
if ((Get-TaskHash $taskParentPath) -ne '35738253af6bd1f41c2a8a6b83233f1007e1ec1d913e234e6d3f3ae971d07707') { throw 'Parent manifest changed' }
$taskParent = Get-Content -LiteralPath $taskParentPath -Raw | ConvertFrom-Json
if ((git rev-parse HEAD) -ne $taskProof.head) { throw 'HEAD changed' }
if (git diff --cached --name-only) { throw 'Index is not empty' }
$taskTargets = @('src/runtime/mapping.cpp','tests/cpp/interpreter_tests.cpp','tests/cpp/dict_scalar_append_index_cases.h')
if ((($taskProof.candidate_file_list | Sort-Object) -join ',') -ne (($taskTargets | Sort-Object) -join ',')) { throw 'Unexpected proposal scope' }
if ($taskProof.raw_before_count -ne 114 -or $taskProof.proposed_recorded_input_count -ne 115) { throw 'Unexpected recorded input counts' }
if (Test-Path -LiteralPath $taskTargets[2]) { throw 'New test target already exists' }
Assert-TaskMap $taskRepo $taskParent.source_snapshot_sha256
Assert-TaskMap $taskRepo $taskProof.raw_before_sha256
Assert-TaskMap (Join-Path $taskParentRoot 'source-snapshot') $taskParent.source_snapshot_sha256
Assert-TaskMap (Join-Path $taskParentRoot 'Release') $taskParent.files_sha256
Assert-TaskMap (Join-Path $taskRepo 'build-repro/main-verify-20261006/Release') $taskParent.files_sha256
Assert-TaskMap (Join-Path $taskRepo 'build-repro/Release') $taskParent.fixed_baseline_sha256
Assert-TaskMap (Join-Path $taskRepo $taskProof.candidate_root) $taskProof.candidate_source_sha256
$taskApplied = @()
try {
    foreach ($target in $taskTargets) {
        Copy-Item -LiteralPath (Join-Path (Join-Path $taskRepo $taskProof.candidate_root) $target) -Destination (Join-Path $taskRepo $target)
        $taskApplied += $target
    }
    Assert-TaskMap $taskRepo $taskProof.candidate_source_sha256
    $taskCandidateSources = [ordered]@{}
    foreach ($entry in $taskParent.source_snapshot_sha256.PSObject.Properties) { $taskCandidateSources[$entry.Name] = $entry.Value }
    foreach ($entry in $taskProof.candidate_source_sha256.PSObject.Properties) { $taskCandidateSources[$entry.Name] = $entry.Value }
    Assert-TaskMap $taskRepo ([PSCustomObject]$taskCandidateSources)
    $taskReceipt = [ordered]@{
        status = 'candidate_applied_not_built_or_validated'; terminal = $true
        source_count = 115; source_sha256 = $taskCandidateSources
        proposal = $Proposal; proposal_sha256 = $ProposalSha256
        parent_manifest_sha256 = Get-TaskHash $taskParentPath
        applied_utc = [DateTime]::UtcNow.ToString('o')
    }
    $taskReceipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath 'doc/performance/data/dict-scalar-append-runtime-index-applied-source-20261008.json' -Encoding utf8NoBOM
    [ordered]@{status=$taskReceipt.status; source_count=115} | ConvertTo-Json
} catch {
    foreach ($target in $taskApplied) {
        $parentFile = Join-Path (Join-Path $taskParentRoot 'source-snapshot') $target
        if (Test-Path -LiteralPath $parentFile) { Copy-Item -LiteralPath $parentFile -Destination (Join-Path $taskRepo $target) }
        elseif ($target -eq 'tests/cpp/dict_scalar_append_index_cases.h') { Remove-Item -LiteralPath (Join-Path $taskRepo $target) }
    }
    throw
}
