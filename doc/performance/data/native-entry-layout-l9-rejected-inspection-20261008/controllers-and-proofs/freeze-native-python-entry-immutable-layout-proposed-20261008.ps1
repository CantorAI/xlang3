$ErrorActionPreference = 'Stop'
$repo = 'D:/CantorAI/xlang3'
$stem = 'scratch/performance/native-python-entry-immutable-layout-proposed-20261008'
$inventoryPath = 'doc/performance/data/sorted-exact-int-s8-registered-source-20261008.json'
$inventorySha = 'f20c304e32b0874c929d22dc72018cbd6d5fd9d124844c1d66d8edcc6a231556'
$validationPath = 'doc/performance/data/sorted-exact-int-s8-full-validation-20261008.json'
$validationSha = 'd4b028b1255f23b11e664bd9e6778b808a7d160d3628e17f39710faa1900d022'
$existing = @('src/internal/xlang3/ir.h','src/executor/xlang_vm/xlang_frame.h',
    'src/executor/xlang_vm/xlang_interpreter.cpp','src/executor/xlang_vm/xlang_vm_loop.cpp',
    'tests/cpp/interpreter_tests.cpp')
$new = @('tests/cpp/immutable_entry_layout_cases.h')
function Hash([string]$path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $repo $path)).Hash.ToLowerInvariant()
}
if ((Hash $inventoryPath) -ne $inventorySha -or (Hash $validationPath) -ne $validationSha) {
    throw 'parent source/validation receipt drift'
}
$inventory = Get-Content -Raw -LiteralPath (Join-Path $repo $inventoryPath) | ConvertFrom-Json
$rawBefore = [ordered]@{}
foreach ($entry in $inventory.source_sha256.PSObject.Properties) {
    $rawBefore[$entry.Name] = Hash $entry.Name
    if ($rawBefore[$entry.Name] -ne $entry.Value) { throw "actual parent drift: $($entry.Name)" }
}
if ($rawBefore.Count -ne 110) { throw 'expected actual S8 source union110' }
$candidate = [ordered]@{}
$normalizedBefore = [ordered]@{}
$patchPieces = [Collections.Generic.List[string]]::new()
foreach ($relative in $existing) {
    $input = "$stem-inputs/$relative"
    $target = "$stem-candidates/$relative"
    $actualLf = [IO.File]::ReadAllText((Join-Path $repo $relative)).Replace("`r`n","`n")
    if ($actualLf -cne [IO.File]::ReadAllText((Join-Path $repo $input))) {
        throw "normalized input mismatch: $relative"
    }
    $normalizedBefore[$relative] = Hash $input
    $candidate[$relative] = Hash $target
    $lines = @(& git -C $repo -c core.autocrlf=false diff --no-index --no-ext-diff --unified=3 -- $input $target)
    if ($LASTEXITCODE -ne 1) { throw "expected bounded candidate delta: $relative" }
    $piece = ($lines -join "`n").Replace("a/$stem-inputs/",'a/').Replace("b/$stem-candidates/",'b/')
    $patchPieces.Add($piece)
}
foreach ($relative in $new) {
    if (Test-Path -LiteralPath (Join-Path $repo $relative)) { throw "new live target exists: $relative" }
    $target = "$stem-candidates/$relative"
    $candidate[$relative] = Hash $target
    $lines = [IO.File]::ReadAllLines((Join-Path $repo $target))
    $blob = (& git -C $repo -c core.autocrlf=false hash-object -- $target).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'new blob hash failed' }
    $heading = @("diff --git a/$relative b/$relative",'new file mode 100644',
        "index 0000000..$blob",'--- /dev/null',"+++ b/$relative", "@@ -0,0 +1,$($lines.Length) @@")
    $patchPieces.Add((($heading + @($lines | ForEach-Object { '+' + $_ })) -join "`n"))
}
$patchPath = "$stem.patch"
[IO.File]::WriteAllText((Join-Path $repo $patchPath),($patchPieces -join "`n") + "`n",[Text.UTF8Encoding]::new($false))
$applyLog = "$stem-apply-check.log"
$applyOutput = @(& git -C $repo apply --check --ignore-whitespace -- $patchPath 2>&1)
$applyExit = $LASTEXITCODE
[IO.File]::WriteAllText((Join-Path $repo $applyLog),($applyOutput -join "`n") + "`n",[Text.UTF8Encoding]::new($false))
if ($applyExit -ne 0) { throw 'bounded apply-check failed' }
foreach ($entry in $rawBefore.GetEnumerator()) {
    if ((Hash $entry.Key) -ne $entry.Value) { throw "source changed during static freeze: $($entry.Key)" }
}
$proof = [ordered]@{
    status='frozen_scratch_only_unapplied_uncompiled_unexecuted'
    design='scratch/performance/native-python-entry-immutable-layout-design-r2-20261008.md'
    design_sha256=Hash 'scratch/performance/native-python-entry-immutable-layout-design-r2-20261008.md'
    parent_inventory=$inventoryPath; parent_inventory_sha256=$inventorySha; parent_source_count=110
    parent_validation=$validationPath; parent_validation_sha256=$validationSha
    raw_before_sha256=$rawBefore; normalized_before_sha256=$normalizedBefore
    existing_owned_targets=$existing; new_owned_targets=$new
    candidate_root=(Join-Path $repo "$stem-candidates")
    input_root=(Join-Path $repo "$stem-inputs")
    candidate_source_sha256=$candidate
    patch_sha256=Hash $patchPath; apply_check_exit=$applyExit; apply_check_log_sha256=Hash $applyLog
    preparer_sha256=Hash 'scratch/performance/freeze-native-python-entry-immutable-layout-proposed-20261008.ps1'
    all_actual_sources_unchanged=$true
    runtime_ast_build_executed=$false
    ownership=@('Immutable map pointer is anchored only by current frame execution_metadata',
        'Save unbinds before moving caches; restore binds after current owner validation',
        'Normal and exceptional pool teardown unbind active and saved layouts before owner release',
        'Monitoring and owning cache payload reset/cleanup remain per activation',
        'No Interpreter lease, cross-activation function identity, mutable cache persistence or new counters')
    meaningful_tests=@('32 actual public native-to-Python driver entries with A/uncached/B/A same-depth restore',
        'Exact current metadata map/max capacity and zero-cache function proves admission',
        'Repeated object arguments and returned values retire without extra function/argument owners',
        'Live __code__ replacement selects current metadata/capacity',
        'Copied IR rejects inherited metadata pointer owner',
        'Existing sparse-storage test preserves logical uncached size and resets dense monitoring')
    targeted_followup=@('Public CPP interpreter target',
        'Unchanged trace/profile/handled/generator/thread fixtures',
        'Unchanged branch direct/sorted callback boundary then original pure-pickle body before full gate')
    scope='setup-only trial hypothesis; no measured gain or acceptance claim'
}
$proofPath = "$stem-provenance.json"
[IO.File]::WriteAllText((Join-Path $repo $proofPath),($proof | ConvertTo-Json -Depth 8) + "`n",[Text.UTF8Encoding]::new($false))
Write-Output ('patch ' + (Hash $patchPath))
Write-Output ('proof ' + (Hash $proofPath))
Write-Output ('cpp ' + $candidate['tests/cpp/immutable_entry_layout_cases.h'])
