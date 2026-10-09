$ErrorActionPreference = 'Stop'
$taskRoot = 'D:/CantorAI/xlang3'
Set-Location -LiteralPath $taskRoot
$stem = 'scratch/performance/native-str-utf8-encode-proposed-20261008'
$targets = @('src/internal/xlang3/builtins.h', 'src/runtime/methods/string_methods.cpp', 'src/runtime/modules/system/codecs_module.cpp')
$utf8 = [System.Text.UTF8Encoding]::new($false)
function Hash-File($path) { (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant() }
function Save-Json($path, $value) {
  [System.IO.File]::WriteAllText((Join-Path $taskRoot $path), (($value | ConvertTo-Json -Depth 30) + "`n"), $utf8)
}

$parentPath = 'build-repro/controls/native-str-utf8-encode-parent-20261008/preserved-release-provenance.json'
$parentSHA = 'cd5035f531a513104f2948286e81d4996bb01a9b6dc4c84353303f370de4bd97'
if ((Hash-File $parentPath) -ne $parentSHA) { throw 'Parent manifest drift' }
$parent = Get-Content -Raw -LiteralPath $parentPath | ConvertFrom-Json
$raw = [ordered]@{}
foreach ($entry in $parent.source_snapshot_sha256.PSObject.Properties) {
  $raw[$entry.Name] = $entry.Value
  if ((Hash-File $entry.Name) -ne $entry.Value) { throw "Live source drift: $($entry.Name)" }
  $archived = Join-Path 'build-repro/controls/native-str-utf8-encode-parent-20261008/source-snapshot' $entry.Name
  if ((Hash-File $archived) -ne $entry.Value) { throw "Archived source drift: $($entry.Name)" }
}
if ($raw.Count -ne 117 -or $parent.file_count -ne 178 -or $parent.object_count -ne 149) { throw 'Parent count drift' }
$before = [ordered]@{}
$candidate = [ordered]@{}
foreach ($path in $targets) {
  $before[$path] = Hash-File (Join-Path ($stem + '-inputs') $path)
  $candidate[$path] = Hash-File (Join-Path ($stem + '-candidates') $path)
  if ($before[$path] -ne $raw[$path]) { throw "Raw target drift: $path" }
}
$diff = & git -c core.autocrlf=false diff --no-index --no-ext-diff --full-index -- ($stem + '-inputs') ($stem + '-candidates')
if ($LASTEXITCODE -ne 1) { throw 'Expected three-target diff' }
$patch = (($diff -join "`n") + "`n").Replace('a/' + $stem + '-inputs/', 'a/').Replace('b/' + $stem + '-candidates/', 'b/')
[System.IO.File]::WriteAllText((Join-Path $taskRoot ($stem + '.patch')), $patch, $utf8)
& git apply --check --ignore-space-change ($stem + '.patch')
if ($LASTEXITCODE -ne 0) { throw 'Read-only apply check failed' }
$testStem = 'scratch/performance/native-str-utf8-encode-tests-proposed-20261008'
$testExisting = @('tests/run_fixtures.py', 'tests/run_fixtures.ps1')
$testNew = @('tests/fixtures/core/native_str_utf8_encode.py', 'tests/fixtures/expected/native_str_utf8_encode.out')
foreach ($path in $testExisting) {
  $before[$path] = Hash-File (Join-Path ($testStem + '-inputs') $path)
  $candidate[$path] = Hash-File (Join-Path ($testStem + '-candidates') $path)
  if ($before[$path] -ne $raw[$path]) { throw "Registration input drift: $path" }
}
foreach ($path in $testNew) {
  if (Test-Path -LiteralPath $path) { throw "New target exists: $path" }
  $candidate[$path] = Hash-File (Join-Path ($testStem + '-candidates') $path)
}
$testDiff = & git -c core.autocrlf=false diff --no-index --no-ext-diff --full-index -- ($testStem + '-inputs') ($testStem + '-candidates')
if ($LASTEXITCODE -ne 1) { throw 'Expected additive test-package diff' }
$testPatch = (($testDiff -join "`n") + "`n").Replace('a/' + $testStem + '-inputs/', 'a/').Replace('b/' + $testStem + '-candidates/', 'b/').Replace('a/' + $testStem + '-candidates/', 'a/')
[System.IO.File]::WriteAllText((Join-Path $taskRoot ($testStem + '.patch')), $testPatch, $utf8)
& git apply --check --ignore-space-change ($testStem + '.patch')
if ($LASTEXITCODE -ne 0) { throw 'Read-only test apply check failed' }
$combinedPath = $stem + '-complete.patch'
[System.IO.File]::WriteAllText((Join-Path $taskRoot $combinedPath), $patch + $testPatch, $utf8)
& git apply --check --ignore-space-change $combinedPath
if ($LASTEXITCODE -ne 0) { throw 'Read-only complete apply check failed' }
$finalCandidateRoot = $stem + '-final-candidates'
$finalInputRoot = $stem + '-final-inputs'
if ((Test-Path -LiteralPath $finalCandidateRoot) -or (Test-Path -LiteralPath $finalInputRoot)) { throw 'Final scratch roots already exist' }
foreach ($path in @($targets) + @($testExisting) + @($testNew)) {
  $sourceRoot = if ($targets -contains $path) { $stem + '-candidates' } else { $testStem + '-candidates' }
  $destination = Join-Path $finalCandidateRoot $path
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
  Copy-Item -LiteralPath (Join-Path $sourceRoot $path) -Destination $destination
  if ((Hash-File $destination) -ne $candidate[$path]) { throw 'Final candidate copy mismatch' }
}
foreach ($path in @($targets) + @($testExisting)) {
  $sourceRoot = if ($targets -contains $path) { $stem + '-inputs' } else { $testStem + '-inputs' }
  $destination = Join-Path $finalInputRoot $path
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
  Copy-Item -LiteralPath (Join-Path $sourceRoot $path) -Destination $destination
  if ((Hash-File $destination) -ne $before[$path]) { throw 'Final input copy mismatch' }
}

$decisionPath = 'scratch/performance/native-str-utf8-encode-trial-decision-proposed-20261008.json'
Save-Json $decisionPath ([ordered]@{
  status = 'decision_frozen_before_any_live_engine_edit_or_candidate_measurement'
  hypothesis = 'Avoid duplicate registry dispatch and tuple/count materialization for guarded native str.encode UTF-8 using the unchanged own native UTF-8 kernel.'
  parent_manifest = $parentPath; parent_manifest_sha256 = $parentSHA
  parent_recorded_sources = 117; parent_release_files = 178; parent_objects = 149
  diagnostic = 'doc/performance/data/native-utf8-encoding-current-vs-cpython3147-20261008.json'
  diagnostic_sha256 = 'edcefb28ec3dee4cfed88d6b69c73ad6f2a5973276a24afd370dc5edf57f06c8'
  screen = [ordered]@{
    focused_semantics_first = $true; pairs = 7; children = 14; alternating_order = $true; retain_all_samples = $true
    original_body = 'Unchanged pickle benchmark function: protocol5,41 outer loops,3 seeded objects,20 dumps each,2460 dumps; strict byte signatures and roundtrips.'
    ratio = 'parent_seconds/candidate_seconds'; median_strictly_greater_than = 1.05
    bootstrap_resamples = 50000; bootstrap_seed = 20261008
    bootstrap_percentiles = 'Linear2.5/97.5 percentiles of resampled seven-pair medians'; lower95_strictly_greater_than = 1.0
    adaptive_counts = $false; retries = $false; outlier_cuts = $false
  }
  after_useful_screen = 'Fresh complete correctness, unchanged default11 fixed-baseline gate21 repeats/5 warmups/0.10, then original official pickle_pure_python protocol5/20 values against CPython3.14.7 reference. Commit only if passing; otherwise preserve and restore parent.'
  exclusions = 'No library translation, byte-loop rewrite, custom-handler parity repair, alias widening, subclass replacement, or gain claim from diagnostic timings or sample counts.'
})
$fixture = 'scratch/performance/native-str-utf8-encode-fixture-proposed-20261008.py'
$expected = 'scratch/performance/native-str-utf8-encode-expected-proposed-20261008.out'
$declaration = @'
// Bytes-only native UTF-8 entry; callers must preserve str argument validation
// and restrict registry bypass to CPython's common UTF-8 spellings/error modes.
bool runtime_encode_utf8(Runtime& runtime, const Value& text,
                         const std::string& errors, Value& out,
                         std::string& error);
'@
$proofPath = $stem + '-provenance.json'
Save-Json $proofPath ([ordered]@{
  status = 'frozen_scratch_proposal_unapplied_unexecuted_unmeasured'
  parent_inventory = 'doc/performance/data/dict-scalar-append-runtime-index-r3-applied-source-20261008.json'
  parent_inventory_sha256 = 'dc1b869fe0475e3bdd017aa3b667f755cb7a4dd7ad881494f8afb34af387f45e'
  parent_inventory_recorded_count = 115
  parent_control_manifest = $parentPath; parent_control_manifest_sha256 = $parentSHA
  parent_recorded_input_count = 117; parent_release_count = 178; parent_object_count = 149
  parent_source_scope = 'Recorded115 plus existing string_methods.cpp/codecs_module.cpp; not all compiled repository sources.'
  raw_before_sha256 = $raw; raw_target_source_sha256 = $before
  candidate_file_list = @($targets) + @($testExisting) + @($testNew)
  existing_owned = @($targets) + @($testExisting); new_owned = $testNew; new_owned_targets = $testNew
  resulting_recorded_source_count = 119; resulting_core_fixture_count = 399
  candidate_root = $taskRoot + '/' + $finalCandidateRoot; raw_input_root = $taskRoot + '/' + $finalInputRoot
  engine_candidate_root = $taskRoot + '/' + $stem + '-candidates'; engine_raw_input_root = $taskRoot + '/' + $stem + '-inputs'
  test_candidate_root = $taskRoot + '/' + $testStem + '-candidates'; test_raw_input_root = $taskRoot + '/' + $testStem + '-inputs'
  candidate_source_sha256 = $candidate; patch = $combinedPath; patch_sha256 = Hash-File $combinedPath
  engine_patch = $stem + '.patch'; engine_patch_sha256 = Hash-File ($stem + '.patch')
  test_patch = $testStem + '.patch'; test_patch_sha256 = Hash-File ($testStem + '.patch')
  engine_targets = $targets; test_targets = @($testExisting) + @($testNew)
  partial_owned_paths = @('src/internal/xlang3/builtins.h')
  partial_owned_policy = 'Stage HEAD plus ONLY this declaration insertion; preserve preexisting unowned dirty header bytes in working source.'
  owned_declaration_anchor = 'void register_codecs_module(Runtime& runtime);'
  owned_declaration_after_anchor = $declaration
  kernel_policy = 'UTF-8 byte loop and codec_info_encode unchanged. External wrapper passes fixed utf_8, pins receiver through exception/output retirement and stages local bytes before publication.'
  admission = 'Exact native String receiver/explicit args; validation before raw ASCII CP-normalized utf8/utf_8 and raw case-sensitive supported error mode checks. Both checks precede existing canonicalization/lowercasing.'
  fallback = 'Existing keyword merger unchanged; validated merged native kwargs may accelerate. Subclass args, u8/cp65001, custom/unknown errors and other encodings preserve original route.'
  observer_policy = 'Existing str.encode native entry dispatch unchanged; helper emits no synthetic native event. Fixture checks boundary counts, not existing callable event-argument parity.'
  semantic_fixture = [ordered]@{
    source = $fixture; source_sha256 = Hash-File $fixture; expected = $expected; expected_sha256 = Hash-File $expected; groups = 8
    proposed_destinations = @('tests/fixtures/core/native_str_utf8_encode.py', 'tests/fixtures/expected/native_str_utf8_encode.out')
    registration = 'Additive once-only native_str_utf8_encode registration in both proposed runner candidates; root CP-first before application.'
    expected_status = 'Proposed semantic transcript, not executed CP output'
    scope = 'Passed diagnostic data/error fields plus bypass/registry/subclass/custom-valid-input/keyword and encode event-boundary assertions; extra groups unexecuted.'
    limits = 'Custom-handler surrogate invocation is a preexisting native-codec limitation, unchanged. No full callable event-argument parity claim.'
  }
  trial_decision = $decisionPath; trial_decision_sha256 = Hash-File $decisionPath
  history = 'No identical measured shortcut found in committed docs and preserved patches. Git d759c5f4/b6efc1cf is compatibility work.'
  cpython_sources = @('https://raw.githubusercontent.com/python/cpython/v3.14.7/Objects/unicodeobject.c', 'https://raw.githubusercontent.com/python/cpython/v3.14.7/Objects/stringlib/codecs.h')
  apply_check_exit = 0; actual_engine_changes = $false; python_execution = $false; ast_execution = $false
  build_execution = $false; runtime_execution = $false; timing_execution = $false
})
$rows = foreach ($path in @(($stem + '.patch'), ($testStem + '.patch'), $combinedPath, $proofPath, $fixture, $expected, $decisionPath)) {
  [pscustomobject]@{Path = $path; SHA256 = Hash-File $path}
}
$rows | Format-List
$candidate | ConvertTo-Json
