$ErrorActionPreference = 'Stop'
$repo = 'D:\CantorAI\xlang3'
$utf8 = [System.Text.UTF8Encoding]::new($false)
function Replace-One([string] $body, [string] $old, [string] $replacement) {
  $at = $body.IndexOf($old, [System.StringComparison]::Ordinal)
  if ($at -lt 0 -or $body.IndexOf($old, $at + $old.Length, [System.StringComparison]::Ordinal) -ge 0) { throw 'Correction anchor not unique' }
  return $body.Remove($at, $old.Length).Insert($at, $replacement)
}
function Write-Fresh([string] $rel, [string] $body) {
  $path = Join-Path $repo $rel
  if (Test-Path -LiteralPath $path) { throw "Already exists: $rel" }
  [System.IO.File]::WriteAllText($path, $body, $utf8)
}
$test = [System.IO.File]::ReadAllText((Join-Path $repo 'scratch/performance/canonical-slot-r4-test-insert-20261008.h'), $utf8).Replace("`r`n", "`n")
$old = @'
  expect_true(result, object_set_attr(owner, "alias", alias_changed, error) &&
      xlang_vm_load_attr_cached(receiver, "alias", cache, found, error) &&
      cache.index == UINT32_MAX && value_is(found, alias_changed),
      "class mutation recomputes negative descriptor identity rather than returning the old value");
'@.Replace("`r`n", "`n")
$new = @'
  expect_true(result, object_set_attr(owner, "alias", alias_changed, error) &&
      xlang_vm_load_attr_cached(receiver, "alias", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index != UINT32_MAX &&
      value_is(found, alias_changed),
      "class mutation returns the new descriptor while old owning cache cleanup stays retryable");
  expect_true(result, xlang_vm_load_attr_cached(receiver, "alias", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index == UINT32_MAX &&
      value_is(found, alias_changed),
      "the following safe warm read recomputes the changed alias's negative shape");
'@.Replace("`r`n", "`n")
$test = Replace-One $test $old $new
$old = @'
  expect_true(result, object_set_attr(owner, "x", descriptor, error) &&
      xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot && found.as.i64 == 47,
      "restoring canonical descriptor invalidates negative eligibility and permits promotion");
'@.Replace("`r`n", "`n")
$new = @'
  expect_true(result, object_set_attr(owner, "x", descriptor, error) &&
      xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index != UINT32_MAX &&
      value_is(found, descriptor),
      "restoring canonical descriptor clears negative eligibility while old owner cleanup stays generic");
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot && found.as.i64 == 47,
      "the following safe canonical hit promotes after restored descriptor cleanup");
'@.Replace("`r`n", "`n")
$test = Replace-One $test $old $new
Write-Fresh 'scratch/performance/canonical-slot-r4-v2-test-insert-20261008.h' $test
$property = [System.IO.File]::ReadAllText((Join-Path $repo 'scratch/performance/canonical-slot-r4-property-cache-insert-20261008.h'), $utf8).Replace("`r`n", "`n")
$old = @'
  expect_true(result, object_set_attr(owner, "x", foreign_descriptor, error) &&
      xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index == UINT32_MAX &&
      !cache.getter_inline && cache.accessor_function == nullptr &&
      value_is(cache.getter_const, retained_constant) && value_is(found, foreign_descriptor),
      "property-to-negative descriptor clears stale accessor while retaining owning constants");
'@.Replace("`r`n", "`n")
$new = @'
  expect_true(result, object_set_attr(owner, "x", foreign_descriptor, error) &&
      xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index != UINT32_MAX &&
      !cache.getter_inline && cache.accessor_function == nullptr &&
      value_is(cache.getter_const, retained_constant) && value_is(found, foreign_descriptor),
      "property-to-foreign cleanup clears stale accessor while retaining retry and owning constants");
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index == UINT32_MAX &&
      !cache.getter_inline && cache.accessor_function == nullptr &&
      value_is(found, foreign_descriptor),
      "the following safe foreign-owner hit caches only the stable negative shape");
'@.Replace("`r`n", "`n")
$property = Replace-One $property $old $new
Write-Fresh 'scratch/performance/canonical-slot-r4-v2-property-cache-insert-20261008.h' $property
$generator = [System.IO.File]::ReadAllText((Join-Path $repo 'scratch/performance/prepare-canonical-slot-r4-proposal-20261008.ps1'), $utf8)
$generator = $generator.Replace('canonical-slot-promotion-r4-20261008', 'canonical-slot-promotion-r4-v2-20261008')
$generator = $generator.Replace('canonical-slot-r4-test-insert-20261008.h', 'canonical-slot-r4-v2-test-insert-20261008.h')
$generator = $generator.Replace('canonical-slot-r4-property-cache-insert-20261008.h', 'canonical-slot-r4-v2-property-cache-insert-20261008.h')
$generator = $generator.Replace('prepare-canonical-slot-r4-proposal-20261008.ps1', 'prepare-canonical-slot-r4-v2-proposal-20261008.ps1')
$generator = $generator.Replace('r4_scratch_proposal_not_applied_not_compiled_not_run', 'r4_v2_scratch_proposal_not_applied_not_compiled_not_run')
Write-Fresh 'scratch/performance/prepare-canonical-slot-r4-v2-proposal-20261008.ps1' $generator
Write-Output 'Preserved original R4 artifacts and generated fresh corrected v2 test fragments/generator.'
