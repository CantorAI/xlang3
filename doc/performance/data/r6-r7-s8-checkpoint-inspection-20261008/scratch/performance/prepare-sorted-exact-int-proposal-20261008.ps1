$ErrorActionPreference = 'Stop'
$root = 'D:\CantorAI\xlang3'
$stem = 'scratch/performance/sorted-exact-int-proposal-20261008'
$relative = 'src/builtins/functional_builtins.cpp'
$utf8 = [System.Text.UTF8Encoding]::new($false)
function Sha([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
$livePath = Join-Path $root $relative
$rawBefore = Sha $livePath
if ($rawBefore -ne 'db0d23b44327ea6ca09d55c6e9351fd75e99cc187b3e0a10a0c7df1b86bddfc3') { throw 'Reviewed sorted source changed' }
$inputPath = Join-Path $root ($stem + '-inputs/' + $relative)
$candidatePath = Join-Path $root ($stem + '-candidates/' + $relative)
$rawPath = Join-Path $root ($stem + '-raw-inputs/' + $relative)
$patchPath = Join-Path $root ($stem + '.patch')
$proofPath = Join-Path $root ($stem + '-provenance.json')
foreach ($path in @($inputPath, $candidatePath, $rawPath, $patchPath, $proofPath)) {
  if (Test-Path -LiteralPath $path) { throw ('Frozen output already exists: ' + $path) }
  [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($path)) | Out-Null
}
$rawBytes = [System.IO.File]::ReadAllBytes($livePath)
[System.IO.File]::WriteAllBytes($rawPath, $rawBytes)
$input = $utf8.GetString($rawBytes).Replace("`r`n", "`n")
$needle = @'
bool sort_entries(Runtime& runtime, std::vector<SortEntry>& entries, bool reverse, std::string& error) {
  bool compare_failed = false;
'@
$replacement = @'
bool sort_entries(Runtime& runtime, std::vector<SortEntry>& entries, bool reverse, std::string& error) {
  // Prove the complete key set before bypassing Python rich comparison. Int64
  // is an immediate scalar: integer subclasses, bool, bigint, floats and all
  // user objects retain the generic comparator below. Key calls and iterable
  // collection have already completed once, with their existing owners intact.
  const bool exact_integer_keys = std::all_of(
      entries.begin(), entries.end(), [](const SortEntry& entry) {
        return entry.key.tag == ValueTag::Int64;
      });
  if (exact_integer_keys) {
    // Already ordered input needs no sort allocation or Value movement. An
    // opposite run can be reversed only when every adjacent key is distinct;
    // reversing equal keys would violate sorted's stable reverse ordering.
    bool ordered = true;
    bool strictly_opposite = entries.size() > 1;
    for (size_t i = 1; i < entries.size(); ++i) {
      const int64_t previous = entries[i - 1].key.as.i64;
      const int64_t current = entries[i].key.as.i64;
      ordered = ordered && (reverse ? previous >= current : previous <= current);
      strictly_opposite = strictly_opposite &&
          (reverse ? previous < current : previous > current);
    }
    if (ordered) return true;
    if (strictly_opposite) {
      std::reverse(entries.begin(), entries.end());
      return true;
    }
    // Homogeneous unordered integer keys still use the existing stable sort,
    // with a callback-free comparator instead of repeated runtime dispatch.
    std::stable_sort(
        entries.begin(), entries.end(),
        [reverse](const SortEntry& lhs, const SortEntry& rhs) {
          return reverse ? rhs.key.as.i64 < lhs.key.as.i64
                         : lhs.key.as.i64 < rhs.key.as.i64;
        });
    return true;
  }
  bool compare_failed = false;
'@
if (($input.Split(@($needle), [System.StringSplitOptions]::None).Count - 1) -ne 1) { throw 'Sort insertion boundary not unique' }
$candidate = $input.Replace($needle, $replacement)
[System.IO.File]::WriteAllText($inputPath, $input, $utf8)
[System.IO.File]::WriteAllText($candidatePath, $candidate, $utf8)
Push-Location $root
try {
  $diffLines = & git diff --no-index --no-ext-diff --text --unified=4 -- ($stem + '-inputs/' + $relative) ($stem + '-candidates/' + $relative)
  if ($LASTEXITCODE -ne 1) { throw 'Expected one proposal diff' }
  $patch = (($diffLines -join "`n") + "`n").Replace('a/' + $stem + '-inputs/', 'a/').Replace('b/' + $stem + '-candidates/', 'b/')
  [System.IO.File]::WriteAllText($patchPath, $patch, $utf8)
  $check = & git apply --check -- $patchPath 2>&1
  if ($LASTEXITCODE -ne 0) { throw ('Static apply check failed: ' + ($check -join "`n")) }
  $head = (& git rev-parse HEAD).Trim()
} finally { Pop-Location }
if ((Sha $livePath) -ne $rawBefore) { throw 'Actual source was changed during scratch generation' }
$dependencies = [ordered]@{}
foreach ($path in @('src/runtime/object_model.cpp', 'src/runtime/value.cpp', 'src/runtime/functional_iterators.cpp', 'src/internal/xlang3/value.h', 'benchmarks/diagnostics/python_callback_boundary.py', 'doc/performance/data/python-callback-boundary-c5-vs-cpython3147-20261008.json', 'tests/fixtures/core/sorted_key_scoped_entry.py', 'tests/fixtures/core/sorted_key_iteration_owner.py')) {
  $dependencies[$path] = Sha (Join-Path $root $path)
}
$proof = [ordered]@{
  status = 'frozen_scratch_proposal_unbuilt_unexecuted'
  created_date = '2026-10-08'
  parent_head = $head
  scope = 'One generic native sorted comparator/run optimization; collection, Python key invocation, generic fallback and all live files unchanged'
  parent_inventory = 'doc/performance/data/call-module-global-binding-r7-applied-source-20261008.json'
  parent_inventory_sha256 = Sha (Join-Path $root 'doc/performance/data/call-module-global-binding-r7-applied-source-20261008.json')
  actual_input_sha256 = [ordered]@{ $relative = $rawBefore }
  actual_input_after_sha256 = [ordered]@{ $relative = Sha $livePath }
  raw_input_sha256 = [ordered]@{ $relative = Sha $rawPath }
  normalized_input_sha256 = [ordered]@{ $relative = Sha $inputPath }
  candidate_sha256 = [ordered]@{ $relative = Sha $candidatePath }
  target_count = 1
  patch_sha256 = Sha $patchPath
  generator_sha256 = Sha (Join-Path $root 'scratch/performance/prepare-sorted-exact-int-proposal-20261008.ps1')
  fixture = 'scratch/performance/sorted-exact-int-fixture-proposed-20261008.py'
  fixture_sha256 = Sha (Join-Path $root 'scratch/performance/sorted-exact-int-fixture-proposed-20261008.py')
  expected = 'scratch/performance/sorted-exact-int-expected-proposed-20261008.out'
  expected_sha256 = Sha (Join-Path $root 'scratch/performance/sorted-exact-int-expected-proposed-20261008.out')
  test_plan = 'scratch/performance/sorted-exact-int-test-plan-proposed-20261008.md'
  test_plan_sha256 = Sha (Join-Path $root 'scratch/performance/sorted-exact-int-test-plan-proposed-20261008.md')
  dependencies_sha256 = $dependencies
  static_apply_check_exit_code = 0
  live_source_unchanged = $true
  fixture_executed = $false
  compiler_or_runtime_executed = $false
}
[System.IO.File]::WriteAllText($proofPath, (($proof | ConvertTo-Json -Depth 10) + "`n"), $utf8)
[ordered]@{ patch_sha256 = Sha $patchPath; proof_sha256 = Sha $proofPath; candidate_sha256 = Sha $candidatePath; fixture_sha256 = $proof.fixture_sha256 } | ConvertTo-Json
