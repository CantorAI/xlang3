$ErrorActionPreference = 'Stop'
$taskRoot = 'D:/CantorAI/xlang3'
Set-Location -LiteralPath $taskRoot
$taskPrefix = 'doc/performance/data/frame-locals-retirement-r4-checkpoint-20261008'
$taskManifest = 'scratch/performance/frame-locals-retirement-r4-publication-proposed-20261008-manifest.json'
$taskNul = 'scratch/performance/frame-locals-retirement-r4-publication-proposed-20261008-paths.nul'
if ((Test-Path -LiteralPath $taskManifest) -or (Test-Path -LiteralPath $taskNul)) { throw 'Refusing frozen publication overwrite' }
$taskRows = [Collections.Generic.List[object]]::new()
$taskDestinations = @{}
function Add-TaskFile([string]$source,[string]$destination,[string]$kind,[string]$expected='') {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or -not $destination.StartsWith('doc/performance/') -or
        $source -match '\.(exe|dll|obj|lib|pdb|exp|pyd|pyc)$') { throw "Invalid publication file: $source" }
    $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expected -and $hash -ne $expected) { throw "Frozen source changed: $source" }
    if ($taskDestinations.ContainsKey($destination)) {
        if ($taskDestinations[$destination].sha256 -ne $hash) { throw "Different duplicate destination: $destination" }
        return
    }
    if ((Test-Path -LiteralPath $destination) -and (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash) {
        throw "Different existing destination: $destination"
    }
    $row = [ordered]@{source=$source;destination=$destination;sha256=$hash;bytes=(Get-Item -LiteralPath $source).Length;kind=$kind}
    $taskDestinations[$destination] = $row
    $taskRows.Add($row)
}
function Add-TaskData([string]$name,[string]$kind,[string]$expected='') {
    Add-TaskFile ('doc/performance/data/'+$name) ('doc/performance/data/'+$name) $kind $expected
}
function Add-TaskSupport([string]$name,[string]$kind='controller') {
    Add-TaskFile ('scratch/performance/'+$name) ($taskPrefix+'/'+$name) $kind
}
Add-TaskFile 'scratch/performance/frame-locals-retirement-checkpoint-report-proposed-20261008.md' 'doc/performance/frame-locals-retirement-checkpoint-20261008.md' 'report'
Add-TaskFile 'scratch/performance/frame-locals-retirement-checkpoint-chart-proposed-20261008.svg' 'doc/performance/frame-locals-retirement-checkpoint-20261008.svg' 'duration_chart'
foreach ($name in @(
    'frame-locals-retirement-r4-proposed-20261008.patch',
    'frame-locals-retirement-r4-proposed-20261008-provenance.json',
    'frame-locals-retirement-r4-proposed-20261008-apply-check.log',
    'rebase-frame-locals-retirement-r4-20261008.ps1',
    'apply-frame-locals-retirement-r4-s8-proposed-20261008.py',
    'apply-frame-locals-retirement-r4-s8-proposed-20261008-provenance.json',
    'prepare-frame-locals-retirement-s8-applier-proposed-20261008.ps1',
    'build-frame-locals-retirement-r4-20261008.cmd',
    'check-frame-locals-retirement-r4-focused-proposed-20261008.py',
    'check-frame-locals-retirement-r4-focused-proposed-20261008-provenance.json',
    'validate-frame-locals-retirement-r4-full-r2-proposed-20261008.py',
    'frame-locals-retirement-r4-full-r2-controller-provenance-proposed-20261008.json',
    'frame-locals-retirement-r4-full-r2-validation-diff-proposed-20261008.patch',
    'prepare-frame-locals-retirement-r4-full-proposed-20261008.ps1',
    'validate-frame-locals-retirement-r4-full-proposed-20261008.py',
    'capture-frame-locals-retained-cpython3147-s8-reference-proposed-20261008.py',
    'frame-locals-retained-reference-manager-provenance-proposed-20261008.json',
    'capture-runtime-frame-context-cpython3147-s8-correctness-r2-proposed-20261008.py',
    'runtime-frame-context-correctness-r2-manager-provenance-20261008.json',
    'preserve-frame-locals-retirement-r4-checkpoint-root-20261008.py',
    'copy-frame-locals-retirement-r4-checkpoint-proposed-20261008.ps1',
    'prepare-frame-locals-retirement-r4-publication-proposed-20261008.ps1')) {
    Add-TaskSupport $name
}
$taskProposal = Get-Content -LiteralPath scratch/performance/frame-locals-retirement-r4-proposed-20261008-provenance.json -Raw | ConvertFrom-Json
foreach ($entry in $taskProposal.candidate_source_sha256.PSObject.Properties) {
    Add-TaskFile ($taskProposal.candidate_root+'/'+$entry.Name) ($taskPrefix+'/candidates/'+$entry.Name) 'reviewed_candidate' $entry.Value
    if ((Get-FileHash -LiteralPath $entry.Name -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value) { throw 'Applied source differs from reviewed candidate' }
}
foreach ($path in $taskProposal.existing_owned_targets) {
    Add-TaskFile ($taskProposal.raw_input_root+'/'+$path) ($taskPrefix+'/before-raw/'+$path) 'referenced_raw_before' $taskProposal.raw_before_sha256.$path
    Add-TaskFile ($taskProposal.normalized_before_root+'/'+$path) ($taskPrefix+'/before-normalized/'+$path) 'referenced_normalized_before'
}
foreach ($name in @(
    'runtime-frame-context-coalesced-fixture-proposed-20261008.py',
    'runtime-frame-context-coalesced-expected-proposed-20261008.out',
    'frame-locals-retained-mapping-fixture-proposed-20261008.py',
    'frame-locals-retained-mapping-expected-proposed-20261008.out',
    'frame-locals-retained-extra-fixture-proposed-20261008.py',
    'frame-locals-retained-extra-expected-proposed-20261008.out')) { Add-TaskSupport $name 'unchanged_strict_fixture' }
Add-TaskData 'frame-locals-retirement-s8-application-20261008-applied-source.json' 'actual_application' 'dc563e2b363dc3992a2c8b323e0f59e953749a715bb4ec97264ef1fa846ab153'
Add-TaskData 'frame-locals-retirement-r4-registered-source-20261008.json' 'registered_source111' 'e696ed5b31449e7ed29c173df79de3b55292597418231f417af887574c60e4c8'
Add-TaskData 'frame-locals-retirement-r4-build-20261008.log' 'actual_build_log' 'd6f11dc2d3457b547d6d9bfa1435776801ea7da21f4665f706ee14ab739f16b1'
Add-TaskData 'frame-locals-retirement-r4-build-terminal-20261008.json' 'actual_build_terminal' 'df8ba2f7b324b6b29173e7622f47fbaa6a8c9ea86560e24db2557b1e8c93ae0a'
foreach ($recordName in @(
    'frame-locals-retirement-r4-focused-20261008.json',
    'frame-locals-retirement-r4-full-validation-20261008.json',
    'runtime-frame-context-cpython3147-s8-correctness-r2-20261008.json',
    'frame-locals-retained-cpython3147-s8-reference-20261008.json')) {
    Add-TaskData $recordName 'actual_receipt'
    $record = Get-Content -LiteralPath ('doc/performance/data/'+$recordName) -Raw | ConvertFrom-Json
    foreach ($phase in $record.phases) {
        foreach ($stream in @('stdout','stderr')) {
            Add-TaskData $phase.($stream+'_log') 'raw_stream' $phase.($stream+'_sha256')
        }
        if ($phase.external_process_watch) {
            Add-TaskData $phase.external_process_watch.log 'raw_timing_watcher' $phase.external_process_watch.sha256
        }
    }
}
$taskFull = Get-Content -LiteralPath doc/performance/data/frame-locals-retirement-r4-full-validation-20261008.json -Raw | ConvertFrom-Json
if (-not $taskFull.terminal -or $taskFull.status -ne 'validated' -or -not $taskFull.full_validated -or
    -not $taskFull.hashes_unchanged -or -not $taskFull.release_tree_unchanged -or
    -not $taskFull.baseline_tree_unchanged -or -not $taskFull.preserved_parent_tree_unchanged) { throw 'Full validated actual result required' }
Add-TaskData $taskFull.fixed_gate.output 'actual_fixed_gate' $taskFull.fixed_gate.sha256
Add-TaskData $taskFull.official_pickle_pure_python.output 'actual_official_pickle20_metadata_values' $taskFull.official_pickle_pure_python.sha256
foreach ($entry in $taskFull.official_pickle_pure_python.partial_evidence_sha256.PSObject.Properties) {
    Add-TaskData $entry.Name 'preserved_official_partial' $entry.Value
}
Add-TaskFile 'build-repro/controls/frame-locals-retirement-s8-application-20261008/preserved-release-provenance.json' ($taskPrefix+'/before-control-manifest.json') 'hash_only_before_control' '29841491e3a6e934db9ef19dfdd01ef3ef71ab14e2cc3333b80213d793ab9e26'
Add-TaskFile 'build-repro/controls/frame-locals-retirement-r4-validated-checkpoint-20261008/preserved-release-provenance.json' ($taskPrefix+'/validated-control-manifest.json') 'hash_only_validated_control' 'b4d5f491b74b1021c18523e61ef6ce70d8002f51a7b1660644d31306f9259c77'
$taskGuards = [ordered]@{
    registered_source=[ordered]@{path='doc/performance/data/frame-locals-retirement-r4-registered-source-20261008.json';sha256='e696ed5b31449e7ed29c173df79de3b55292597418231f417af887574c60e4c8'}
    full_validation=[ordered]@{path='doc/performance/data/frame-locals-retirement-r4-full-validation-20261008.json';sha256='b28809dcfc420906e3c3f656fdd9584140d0a80d778673a43342799ea64e75ec'}
    focused=[ordered]@{path='doc/performance/data/frame-locals-retirement-r4-focused-20261008.json';sha256='e193773b550ec36a0c07aad862d363f4af5467f0d1b05d55af916d945d706a26'}
    build=[ordered]@{path='doc/performance/data/frame-locals-retirement-r4-build-terminal-20261008.json';sha256='df8ba2f7b324b6b29173e7622f47fbaa6a8c9ea86560e24db2557b1e8c93ae0a'}
    validated_manifest=[ordered]@{path='build-repro/controls/frame-locals-retirement-r4-validated-checkpoint-20261008/preserved-release-provenance.json';sha256='b4d5f491b74b1021c18523e61ef6ce70d8002f51a7b1660644d31306f9259c77'}
}
foreach ($entry in $taskGuards.Values) {
    if ((Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) { throw 'Frozen guard changed' }
}
$taskExternal = [Collections.Generic.List[object]]::new()
foreach ($path in @(
    'doc/performance/data/pyperformance-cpython3147-live-eval-full-fast-20261007.json',
    'doc/performance/data/pyperformance-cpython3147-live-eval-full-fast-20261007-provenance.json',
    'doc/performance/data/sorted-exact-int-s8-full-validation-20261008.json',
    'doc/performance/data/sorted-exact-int-s8-registered-source-20261008.json',
    'doc/performance/data/callee-module-owner-r10-withdrawn-unmeasured-duplicate-restored-s8-20261008.json')) {
    $taskExternal.Add([ordered]@{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant();preserved_without_recopy=$true})
}
$taskFiles = @($taskRows | Sort-Object destination)
$taskSelfDestination = $taskPrefix+'/publication-manifest.json'
$taskPaths = @($taskFiles.destination) + @($taskSelfDestination)
$taskNulBytes = [Text.UTF8Encoding]::new($false).GetBytes(([string]::Join([char]0,$taskPaths)+[char]0))
[IO.File]::WriteAllBytes((Join-Path $taskRoot $taskNul),$taskNulBytes)
$taskPlan = [ordered]@{
    status='frozen_doc_only_validated_r4_checkpoint_publication_plan'
    parent_head=(& git --no-optional-locks rev-parse HEAD).Trim()
    recorded_source_count=111
    release_file_count=178
    fixed_accepted_baseline_file_count=177
    source_scope='111 recorded source files, not a complete list of every compiled repository source; measured local dirty bytes remain preserved and clean main alone is not claimed a byte-reproducer.'
    owned_engine_targets=@($taskProposal.candidate_file_list)
    engine_staging_included=$false
    binaries_included=$false
    inspection_source_scope='Three exact reviewed candidates; two referenced raw-before files and their normalized snapshots. No giant all-sources or Release archive in Git.'
    row_count=$taskFiles.Count
    document_path_count=$taskPaths.Count
    files=$taskFiles
    plan_publication_destination=$taskSelfDestination
    doc_only_nul_path=$taskNul
    doc_only_nul_sha256=(Get-FileHash -LiteralPath $taskNul -Algorithm SHA256).Hash.ToLowerInvariant()
    guards=$taskGuards
    historical_references=@($taskExternal)
    result_scope='Return/unwind materialized locals alias update, extra-key preservation, deleted names, deferred old-owner release outside registry mutex; not complete CPFrameLocalsProxy, no new full97 or paired R4/S8 gain.'
    validation=[ordered]@{focused=10;core=398;compat_sections=11;expected_failures=3;ctest=9;sqlite_api=2;gate_cases=11;gate_repeats=21;gate_warmup=5;gate_threshold=0.10;official_pickle_values=20;official_protocol=5}
    official_comparison=[ordered]@{saved_cp_mean_seconds=0.00025749786718506587;fresh_x_mean_seconds=0.005460743000294315;fresh_x_sample_sd_seconds=0.00011781626857313782;cp_over_x_speed=0.0471543647;x_over_cp_time=21.20694;unpaired=$true;pyperf_stability_warning_retained=$true;no_cp_win=$true}
    attributes_guidance='Root stages only needed HEAD-plus-owned text/binary-preservation patterns; never stage the dirty working .gitattributes wholesale. This plan/copy script never writes .gitattributes.'
    created_utc=[DateTime]::UtcNow.ToString('o')
}
[IO.File]::WriteAllText((Join-Path $taskRoot $taskManifest),(($taskPlan | ConvertTo-Json -Depth 12)+"`n"),[Text.UTF8Encoding]::new($false))
Get-FileHash -Algorithm SHA256 -LiteralPath $taskManifest,$taskNul
Write-Output "Prepared $($taskFiles.Count) exact rows and $($taskPaths.Count) doc-only pathspec entries; no copies/staging/live writes."
