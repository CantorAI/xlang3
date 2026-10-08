$ErrorActionPreference = 'Stop'
$repo = 'D:\CantorAI\xlang3'
Set-Location -LiteralPath $repo
$utf8 = [System.Text.UTF8Encoding]::new($false)
$base = 'scratch/performance/canonical-slot-promotion-r4-20261008'
$patchRel = $base + '.patch'
$logRel = $base + '-apply-check.log'
$provenanceRel = $base + '-provenance.json'
$engineRel = 'src/executor/xlang_vm/xlang_vm_attr.cpp'
$testRel = 'tests/cpp/canonical_slot_read_cases.h'
$engineSource = $base + '-engine-source.cpp'
$engineCandidate = $base + '-engine-candidate.cpp'
$testSource = $base + '-test-source.h'
$testCandidate = $base + '-test-candidate.h'
$helperRel = 'scratch/performance/canonical-slot-r4-helper-20261008.cpp'
$testInsertRel = 'scratch/performance/canonical-slot-r4-test-insert-20261008.h'
$unsafeInsertRel = 'scratch/performance/canonical-slot-r4-unsafe-retry-insert-20261008.h'
$propertyInsertRel = 'scratch/performance/canonical-slot-r4-property-cache-insert-20261008.h'
foreach ($fresh in @($patchRel, $logRel, $provenanceRel,
    $engineSource, $engineCandidate, $testSource, $testCandidate)) {
  if (Test-Path -LiteralPath $fresh) { throw "Artifact already exists: $fresh" }
}
$head = (& git rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Could not read HEAD' }
$engineBefore = (Get-FileHash -Algorithm SHA256 -LiteralPath $engineRel).Hash.ToLowerInvariant()
$testBefore = (Get-FileHash -Algorithm SHA256 -LiteralPath $testRel).Hash.ToLowerInvariant()
Copy-Item -LiteralPath $engineRel -Destination $engineSource
Copy-Item -LiteralPath $testRel -Destination $testSource
function Read-Text([string] $rel) {
  return [System.IO.File]::ReadAllText((Join-Path $repo $rel), $utf8)
}
function Replace-One([string] $body, [string] $old, [string] $replacement) {
  $at = $body.IndexOf($old, [System.StringComparison]::Ordinal)
  if ($at -lt 0 -or $body.IndexOf($old, $at + $old.Length, [System.StringComparison]::Ordinal) -ge 0) {
    throw 'Replacement anchor is not unique'
  }
  return $body.Remove($at, $old.Length).Insert($at, $replacement)
}
function Insert-After-Line([string] $body, [string] $anchor, [string] $addition) {
  $at = $body.IndexOf($anchor, [System.StringComparison]::Ordinal)
  if ($at -lt 0 -or $body.IndexOf($anchor, $at + $anchor.Length, [System.StringComparison]::Ordinal) -ge 0) {
    throw 'Insertion anchor is not unique'
  }
  $lineEnd = $body.IndexOf("`n", $at + $anchor.Length, [System.StringComparison]::Ordinal)
  if ($lineEnd -lt 0) { throw 'Insertion anchor lacks newline' }
  return $body.Insert($lineEnd + 1, "`n" + $addition)
}
$engine = Read-Text $engineRel
$helperAt = $engine.IndexOf('// Resolve canonical initialized slot storage once', [System.StringComparison]::Ordinal)
$helperEnd = $engine.IndexOf('XLANG3_NOINLINE bool xlang_vm_resolve_method_value', [System.StringComparison]::Ordinal)
if ($helperAt -lt 0 -or $helperEnd -le $helperAt) { throw 'Helper range is missing' }
$engine = $engine.Remove($helperAt, $helperEnd - $helperAt).Insert($helperAt, (Read-Text $helperRel))
$warmOld = @'
      if (try_promote_canonical_slot_read(
              *instance, *klass, name, cache.value, cache, out)) return true;
'@.Replace("`r`n", "`n")
$warmNew = @'
      if (cache.index != kNoCanonicalSlotPromotion) {
        const auto promotion = try_promote_canonical_slot_read(
            *instance, *klass, name, cache.value, cache, out);
        if (promotion == CanonicalSlotPromotion::Promoted) return true;
        if (promotion == CanonicalSlotPromotion::ShapeIneligible)
          cache.index = kNoCanonicalSlotPromotion;
      }
'@.Replace("`r`n", "`n")
# Match the existing file's line endings locally, leaving unrelated bytes intact.
$engine = Replace-One $engine ($warmOld.Replace("`n", "`r`n")) ($warmNew.Replace("`n", "`r`n"))
$coldOld = @'
        if (try_promote_canonical_slot_read(
                *instance, *klass, name, descriptor, cache, out)) return true;
'@.Replace("`r`n", "`n")
$coldNew = @'
        const auto promotion = try_promote_canonical_slot_read(
            *instance, *klass, name, descriptor, cache, out);
        if (promotion == CanonicalSlotPromotion::Promoted) return true;
'@.Replace("`r`n", "`n")
$engine = Replace-One $engine ($coldOld.Replace("`n", "`r`n")) ($coldNew.Replace("`n", "`r`n"))
$installOld = @'
        cache.kind = AttrSiteKind::Descriptor;
        cache.owner = &klass->header;
        cache.version = klass->version;
        value_assign_fast(cache.value, descriptor);
        value_assign_fast(out, descriptor);
'@.Replace("`r`n", "`n")
$installNew = @'
        // Publishing a changed descriptor under fresh guards must not expose
        // weak accessor pointers from the old property. Disable scalar flags
        // before its owning value can release a function or run a finalizer;
        // owning accessor constants keep their normal cache-cleanup lifetime.
        if (cache.owner != &klass->header || cache.version != klass->version ||
            !value_is(cache.value, descriptor)) {
          cache.getter_inline = false;
          cache.setter_inline = false;
          cache.deleter_inline = false;
          cache.property_attr_name = nullptr;
          cache.class_value = nullptr;
          cache.accessor_function = nullptr;
          cache.accessor_code_version = 0;
        }
        cache.kind = AttrSiteKind::Descriptor;
        cache.owner = &klass->header;
        cache.version = klass->version;
        cache.index = promotion == CanonicalSlotPromotion::ShapeIneligible
            ? kNoCanonicalSlotPromotion : 0;
        value_assign_fast(cache.value, descriptor);
        value_assign_fast(out, descriptor);
'@.Replace("`r`n", "`n")
$engine = Replace-One $engine ($installOld.Replace("`n", "`r`n")) ($installNew.Replace("`n", "`r`n"))
$test = Read-Text $testRel
$test = Insert-After-Line $test '"canonical descriptor restores after foreign-owner fallback");' (Read-Text $testInsertRel)
$test = Insert-After-Line $test '"unsafe old cache values must use original cleanup and descriptor entry");' ((Read-Text $unsafeInsertRel) + (Read-Text $propertyInsertRel))
[System.IO.File]::WriteAllText((Join-Path $repo $engineCandidate), $engine, $utf8)
[System.IO.File]::WriteAllText((Join-Path $repo $testCandidate), $test, $utf8)
function Unified-Diff([string] $oldCopy, [string] $newCopy, [string] $target) {
  $lines = & git diff --no-index --no-ext-diff --no-prefix -- $oldCopy $newCopy
  $exit = $LASTEXITCODE
  if ($exit -ne 1) { throw "Expected unified diff, exit $exit" }
  for ($index = 0; $index -lt $lines.Count; $index++) {
    if ($lines[$index].StartsWith('diff --git ')) { $lines[$index] = "diff --git a/$target b/$target" }
    elseif ($lines[$index].StartsWith('--- ')) { $lines[$index] = "--- a/$target" }
    elseif ($lines[$index].StartsWith('+++ ')) { $lines[$index] = "+++ b/$target" }
  }
  return ($lines -join "`n") + "`n"
}
$patch = (Unified-Diff $engineSource $engineCandidate $engineRel) +
    (Unified-Diff $testSource $testCandidate $testRel)
[System.IO.File]::WriteAllText((Join-Path $repo $patchRel), $patch, $utf8)
$checkOutput = & git apply --check -- $patchRel 2>&1
$checkExit = $LASTEXITCODE
[System.IO.File]::WriteAllText((Join-Path $repo $logRel),
    ("command: git apply --check -- $patchRel`nexit: $checkExit`n" +
      (($checkOutput | ForEach-Object { $_.ToString() }) -join "`n") + "`n"), $utf8)
$engineAfter = (Get-FileHash -Algorithm SHA256 -LiteralPath $engineRel).Hash.ToLowerInvariant()
$testAfter = (Get-FileHash -Algorithm SHA256 -LiteralPath $testRel).Hash.ToLowerInvariant()
if ($engineBefore -ne $engineAfter -or $testBefore -ne $testAfter) { throw 'Current engine/test sources changed during preparation' }
$artifacts = foreach ($rel in @($engineRel, $testRel, $engineSource, $engineCandidate,
    $testSource, $testCandidate, $helperRel, $testInsertRel, $unsafeInsertRel, $propertyInsertRel,
    'scratch/performance/prepare-canonical-slot-r4-proposal-20261008.ps1', $patchRel, $logRel)) {
  [ordered]@{ path = $rel; bytes = (Get-Item -LiteralPath $rel).Length;
    sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $rel).Hash.ToLowerInvariant() }
}
$provenance = [ordered]@{
  status = 'r4_scratch_proposal_not_applied_not_compiled_not_run'
  head = $head
  targets = @($engineRel, $testRel)
  engine_source_sha256_before = $engineBefore
  engine_source_sha256_after = $engineAfter
  test_source_sha256_before = $testBefore
  test_source_sha256_after = $testAfter
  apply_check_command = @('git', 'apply', '--check', '--', $patchRel)
  apply_check_exit = $checkExit
  artifacts = @($artifacts)
}
[System.IO.File]::WriteAllText((Join-Path $repo $provenanceRel),
    (($provenance | ConvertTo-Json -Depth 6) + "`n"), $utf8)
if ($checkExit -ne 0) { throw "Mechanical R4 patch check failed: $checkExit" }
Write-Output "R4 proposal mechanically checked against HEAD $head; current engine/test files unchanged."
