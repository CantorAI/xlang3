$ErrorActionPreference = 'Stop'
$taskRepo = 'D:/CantorAI/xlang3'
$taskStem = 'scratch/performance/dict-scalar-append-runtime-index-r2-proposed-20261008'
$taskInventory = 'doc/performance/data/frame-locals-retirement-r4-registered-source-20261008.json'
$taskInventoryHash = 'e696ed5b31449e7ed29c173df79de3b55292597418231f417af887574c60e4c8'
$taskOwned = @('src/runtime/mapping.cpp', 'tests/cpp/interpreter_tests.cpp', 'tests/cpp/dict_scalar_append_index_cases.h')
$taskExisting = @('src/runtime/mapping.cpp', 'tests/cpp/interpreter_tests.cpp')
$taskNew = 'tests/cpp/dict_scalar_append_index_cases.h'
$taskSupplement = [ordered]@{
    'src/runtime/mapping.cpp' = '81dc9042e1e26874b6a02791b3f62656e5147b8321ea9e87fbe80b53fdac2f74'
    'src/internal/xlang3/mapping.h' = '43f8bbc91e2f224dde767d4f785a64e2c3429b269113ba85dd613eac297c3857'
    'src/runtime/value.cpp' = '18be33a526dbcc6627490a12a68aefc9e168d42afbf238ce38fa240e74bba0e0'
}
$taskUntouched = @('tests/cpp/runtime_value_tests.cpp',
    'scratch/performance/dict-lazy-index-prerequisites-fixture-proposed-20261008.py',
    'scratch/performance/dict-lazy-index-prerequisites-expected-proposed-20261008.out',
    'scratch/performance/dict-equivalent-numeric-keys-isolated-proposed-20261008.py',
    'scratch/performance/dict-equivalent-numeric-keys-isolated-expected-proposed-20261008.out',
    'scratch/performance/dict-numeric-subclass-equality-isolated-proposed-20261008.py',
    'scratch/performance/dict-numeric-subclass-equality-isolated-expected-proposed-20261008.out',
    'scratch/performance/dict-replacement-publication-fixture-proposed-20261008.py',
    'scratch/performance/dict-replacement-publication-expected-proposed-20261008.out')
function Get-TaskHash([string]$Relative) {
    (Get-FileHash -LiteralPath (Join-Path $taskRepo $Relative) -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Write-TaskText([string]$Relative, [string]$Text) {
    $taskPath = Join-Path $taskRepo $Relative
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($taskPath)) | Out-Null
    [IO.File]::WriteAllText($taskPath, $Text, [Text.UTF8Encoding]::new($false))
}
if ((Get-TaskHash $taskInventory) -ne $taskInventoryHash) { throw 'parent inventory drift' }
if (Test-Path -LiteralPath (Join-Path $taskRepo ($taskStem + '-provenance.json'))) { throw 'frozen proof already exists' }
if (Test-Path -LiteralPath (Join-Path $taskRepo $taskNew)) { throw 'new target already exists' }
$taskParent = Get-Content -LiteralPath (Join-Path $taskRepo $taskInventory) -Raw | ConvertFrom-Json -AsHashtable
if ($taskParent.source_count -ne 111 -or $taskParent.source_sha256.Count -ne 111) { throw 'parent schema' }
$taskBefore = [ordered]@{}
foreach ($taskPath in ($taskParent.source_sha256.Keys | Sort-Object)) { $taskBefore[$taskPath] = $taskParent.source_sha256[$taskPath] }
foreach ($taskPath in $taskSupplement.Keys) {
    if ($taskBefore.Contains($taskPath)) { throw 'supplement unexpectedly in parent' }
    $taskBefore[$taskPath] = $taskSupplement[$taskPath]
}
if ($taskBefore.Count -ne 114) { throw 'supplemented parent count' }
$taskUnchanged = [ordered]@{}
foreach ($taskPath in $taskUntouched) { $taskUnchanged[$taskPath] = Get-TaskHash $taskPath }
$taskCandidates = [ordered]@{}
foreach ($taskPath in $taskOwned) { $taskCandidates[$taskPath] = Get-TaskHash ($taskStem + '-candidates/' + $taskPath) }
foreach ($taskPath in $taskBefore.Keys) {
    if ((Get-TaskHash $taskPath) -ne $taskBefore[$taskPath]) { throw "before input drift: $taskPath" }
    $taskSnapshot = Join-Path $taskRepo ($taskStem + '-inputs/' + $taskPath)
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($taskSnapshot)) | Out-Null
    [IO.File]::Copy((Join-Path $taskRepo $taskPath), $taskSnapshot, $false)
    if ((Get-TaskHash ($taskStem + '-inputs/' + $taskPath)) -ne $taskBefore[$taskPath]) { throw 'snapshot copy drift' }
}
$taskNormalized = [ordered]@{}
foreach ($taskPath in $taskExisting) {
    $taskText = [IO.File]::ReadAllText((Join-Path $taskRepo $taskPath)).Replace("`r`n", "`n")
    Write-TaskText ($taskStem + '-normalized-before/' + $taskPath) $taskText
    $taskNormalized[$taskPath] = Get-TaskHash ($taskStem + '-normalized-before/' + $taskPath)
}
$taskDiff = & git --no-optional-locks -C $taskRepo diff --no-index --no-ext-diff -- ($taskStem + '-normalized-before') ($taskStem + '-candidates')
if ($LASTEXITCODE -ne 1) { throw 'expected bounded source diff' }
$taskPatch = (($taskDiff -join "`n") + "`n").Replace(('a/' + $taskStem + '-normalized-before/'), 'a/').Replace(('b/' + $taskStem + '-candidates/'), 'b/')
Write-TaskText ($taskStem + '.patch') $taskPatch
& git --no-optional-locks -C $taskRepo apply --check --ignore-whitespace -- ($taskStem + '.patch')
if ($LASTEXITCODE -ne 0) { throw 'read-only apply check failed' }
$taskAfter = [ordered]@{}
foreach ($taskPath in $taskBefore.Keys) {
    $taskAfter[$taskPath] = Get-TaskHash $taskPath
    if ($taskAfter[$taskPath] -ne $taskBefore[$taskPath]) { throw "actual input changed: $taskPath" }
}
foreach ($taskPath in $taskUntouched) { if ((Get-TaskHash $taskPath) -ne $taskUnchanged[$taskPath]) { throw 'preserved dirty/fixture changed' } }
$taskEvidencePaths = @($taskInventory,
    'scratch/performance/dict-scalar-append-freshness-trial-decision-root-20261008.json',
    'doc/performance/data/dict-scalar-append-freshness-diagnostic-r3-20261008.json',
    'doc/performance/pickle-memo-index-freshness-investigation-20261008.md',
    'doc/performance/data/frame-locals-retirement-r4-full-validation-20261008.json',
    'doc/performance/dict-intrinsic-index-checkpoint-20261007.md',
    'doc/performance/set-growing-index-20261006.md',
    'doc/performance/data/dict-numeric-isolated-cpython3147-s8-reference-20261008.json',
    'doc/performance/data/dict-replacement-publication-cpython3147-s8-reference-20261008.json',
    'build-repro/main-verify-20261006/CMakeFiles/xlang3_runtime.dir/Release/exports.def')
$taskEvidence = [ordered]@{}
foreach ($taskPath in $taskEvidencePaths) { $taskEvidence[$taskPath] = Get-TaskHash $taskPath }
$taskHead = (& git --no-optional-locks -C $taskRepo rev-parse HEAD).Trim()
$taskProof = [ordered]@{
    status = 'frozen_scratch_proposal_pending_independent_review_and_root_execution'
    scored = $false; engine_applied = $false; runtime_executed = $false; build_executed = $false
    supersedes_held_r1 = [ordered]@{
        patch = 'scratch/performance/dict-scalar-append-runtime-index-proposed-20261008.patch'
        patch_sha256 = 'acc994d8e4bc5f37603aadf5d84d3bd07931a757119b05d09d48a50b40a08b83'
        proof_sha256 = 'f8eff392a0ec40719c5807207c7aadbf1114c6aacf247788d6c15da9f8a17ea9'
        reason = 'Integer flat growth can inspect stored Instance payloads through native attribute hooks; R1 cannot restore captured runtime freshness across that callback boundary.'
        preserved = $true
    }
    head = $taskHead
    parent_inventory = $taskInventory; parent_inventory_sha256 = $taskInventoryHash
    parent_registered_source_count = 111
    supplemental_native_inputs_sha256 = $taskSupplement
    raw_before_count = 114; proposed_recorded_input_count = 115
    inventory_scope = '111 recorded parent sources plus three separately pinned critical native inputs; not every compiled repository source'
    raw_input_root = ($taskStem + '-inputs'); normalized_before_root = ($taskStem + '-normalized-before')
    candidate_root = ($taskStem + '-candidates')
    candidate_file_list = $taskOwned; existing_owned_targets = $taskExisting; new_owned_targets = @($taskNew)
    candidate_source_sha256 = $taskCandidates; raw_before_sha256 = $taskBefore
    normalized_before_sha256 = $taskNormalized; actual_after_sha256 = $taskAfter
    preserved_unowned_and_fixtures_sha256 = $taskUnchanged
    patch = ($taskStem + '.patch'); patch_sha256 = Get-TaskHash ($taskStem + '.patch')
    apply_check_exit_code = 0; all_actual_inputs_unchanged = $true
    evidence_sha256 = $taskEvidence
    history_review = @('Read prior intrinsic dict index checkpoint and preserved git14f81f72 mapping patch: tuple/bytes write and intrinsic read admission, not scalar early-append maintenance.',
        'Read git3f44b72c mapping patch: captured intrinsic get shortcut, not append maintenance.',
        'Read accepted set-growing report and set_note_append source: distinct set content-version/hash+identity chain maintenance; no repeated dict experiment found.',
        'Searched doc/performance Markdown and patches for runtime_hash_index, runtime_hash_indexed_entry_count; only prior intrinsic, CPU source observation and frame-retirement invalidation matched.')
    allocation_and_ownership_proof = @('Accepted append boundary captures previous runtime freshness and independent intrinsic checked state, then invalidates all three counts before Value copies or vector/map allocations.',
        'Only exact physical Int64/native String extends buckets; nonphysical numeric append cannot publish either certificate. Integer geometric flat growth additionally requires a current checked intrinsic=true proof; false/unknown growth leaves runtime buckets stale, no-growth mixed appends still extend.',
        'Value copy constructors pin container and incoming key/item, including borrowed entry aliases, through all allocations and last dict/index access.',
        'Authoritative entry append precedes runtime bucket insertion and flat-index completion. Counts restore only after all complete. Throws leave runtime/intrinsic certificates unknown; no new callback/reclassification or allocation exception translation.',
        'Current intrinsic true and false are extended independently; unknown remains unknown. Previously stale bucket storage is unchanged until existing runtime lookup rebuilds it.',
        'No entry/bucket references survive local owner cleanup; buckets store scalar locations only. Existing search/equality/overwrite/module-backed/generic callback code is unchanged.')
    tests = @('Public exported DLL mapping_set_item, mapping_get_item_runtime, value_hash_key, clear/delete/copy; actual exports.def pinned.',
        '64 Int64 and64String append/get cycles verify fresh buckets and unique complete entry locations BEFORE get can repair metadata.',
        'Existing integer-like callback key produces checked-false proof;64native appends retain false, runtime staleness occurs only at geometric flat growth and stored hash callbacks are unchanged between growth boundaries.',
        'Native getter hook clears only scalar runtime buckets while flat indexed_entry_count is invalid: mixed growth must stay stale instead of certifying an empty bucket map; existing entry-vector reentry defects are not exercised.',
        'Cold/stale and independent unknown/positive/negative certificate cases, bool refusal, same-size delete+append invalidation, clear/copy/recycle, scalar overwrite/order, borrowed and vector-entry aliases.',
        'No private VM-template copies, mocks, new exports, ABI fields, runtime_value_tests edits, Python fixture weakening, or claimed complete numeric/callback repair.')
    root_execution_plan = @('Preserve validated R4 complete fixed Release178 and supplemented114 raw inputs before applying exactly three frozen targets.',
        'Root fixed-path Release build, then actual registered public C++ suite plus relevant dict/get cases and original Python fixtures.',
        'Use predeclared root decision05aad9b5: seven balanced original-body pairs, median parent/candidate>1.05 and bootstrap95 lower>1.0; retain all raw.',
        'Only after useful screen: fresh full correctness, unchanged default11gate21/5/.10 against fixed177, original official pure-Python pickle20/300. Root alone executes; no claimed passes here.')
    existing_unrepaired_limits = @('Numeric equivalent literal/DictSet construction and numeric subclass equality fixtures previously failed S8; preserved unchanged.',
        'Ordinary scalar/string overwrite can release old value before publication; strict replacement fixture first Int64 event assertion failed S8, actual event contents unknown and later groups unreached. This append-only proposal does not repair it.',
        'Generic stored-key callback mutation/reentry and dict initialization behavior are unchanged; no whole-set reclassification or whole-suite win claimed.')
}
Write-TaskText ($taskStem + '-provenance.json') (($taskProof | ConvertTo-Json -Depth 12) + "`n")
[ordered]@{patch_sha256=(Get-TaskHash ($taskStem + '.patch')); proof_sha256=(Get-TaskHash ($taskStem + '-provenance.json')); candidates=$taskCandidates; before_count=$taskBefore.Count} | ConvertTo-Json -Depth 4
