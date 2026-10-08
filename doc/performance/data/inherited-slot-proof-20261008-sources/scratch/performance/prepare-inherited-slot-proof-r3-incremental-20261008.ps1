$ErrorActionPreference = 'Stop'
$repo = 'D:\CantorAI\xlang3'
Set-Location -LiteralPath $repo
$utf8 = [System.Text.UTF8Encoding]::new($false)
$base = 'scratch/performance/inherited-slot-proof-r3-incremental-20261008'
$patchRel = $base + '.patch'
$logRel = $base + '-apply-check.log'
$provenanceRel = $base + '-provenance.json'
$targets = @('src/executor/xlang_vm/xlang_vm_attr.cpp', 'tests/cpp/canonical_slot_read_cases.h')
foreach ($rel in @($patchRel, $logRel, $provenanceRel, ($base + '-sources'), ($base + '-candidates'))) {
 if (Test-Path -LiteralPath $rel) { throw "Fresh incremental artifact exists: $rel" }
}
function Replace-Exact([string] $body, [string] $old, [string] $new) {
 $old = $old.Replace("`r`n", "`n")
 $new = $new.Replace("`r`n", "`n")
 $at = $body.IndexOf($old, [System.StringComparison]::Ordinal)
 if ($at -lt 0 -or $body.IndexOf($old, $at + $old.Length, [System.StringComparison]::Ordinal) -ge 0) { throw 'Nonunique incremental anchor' }
 return $body.Remove($at, $old.Length).Insert($at, $new)
}
$head = (& git rev-parse HEAD).Trim()
$inventory = @()
$patch = ''
foreach ($rel in $targets) {
 $before = (Get-FileHash -Algorithm SHA256 -LiteralPath $rel).Hash.ToLowerInvariant()
 $source = $base + '-sources/' + $rel
 $candidate = $base + '-candidates/' + $rel
 [System.IO.Directory]::CreateDirectory((Join-Path $repo (Split-Path $source))) | Out-Null
 [System.IO.Directory]::CreateDirectory((Join-Path $repo (Split-Path $candidate))) | Out-Null
 Copy-Item -LiteralPath $rel -Destination $source
 $body = [System.IO.File]::ReadAllText((Join-Path $repo $rel), $utf8).Replace("`r`n", "`n")
 if ($rel.EndsWith('xlang_vm_attr.cpp')) {
  $body = Replace-Exact $body '    const auto owner_index = owner->instance_slot_indices.find(name);' @'
    // VM descriptor dispatch checks raw slot->index for missing/getattr before
    // name remapping. A displaced receiver index must retain that original path.
    if (index != slot->index) return CanonicalSlotPromotion::ShapeIneligible;
    const auto owner_index = owner->instance_slot_indices.find(name);
'@
 } else {
  $insert = [System.IO.File]::ReadAllText((Join-Path $repo 'scratch/performance/inherited-slot-proof-displaced-CPP-20261008.h'), $utf8).Replace("`r`n", "`n")
  $body = Replace-Exact $body 'inline void check_canonical_slot_read_cases(CaseResult& result) {' ($insert + 'inline void check_canonical_slot_read_cases(CaseResult& result) {')
  $body = Replace-Exact $body '  check_inherited_slot_declaration_proof(result);' "  check_inherited_slot_declaration_proof(result);`n  check_displaced_inherited_slot_fallback(result);"
 }
 [System.IO.File]::WriteAllText((Join-Path $repo $candidate), $body, $utf8)
 $lines = & git diff --no-index --no-ext-diff --no-prefix -- $source $candidate
 if ($LASTEXITCODE -ne 1) { throw 'Expected incremental difference' }
 for ($i = 0; $i -lt $lines.Count; $i++) {
  if ($lines[$i].StartsWith('diff --git ')) { $lines[$i] = "diff --git a/$rel b/$rel" }
  elseif ($lines[$i].StartsWith('--- ')) { $lines[$i] = "--- a/$rel" }
  elseif ($lines[$i].StartsWith('+++ ')) { $lines[$i] = "+++ b/$rel" }
 }
 $patch += ($lines -join "`n") + "`n"
 $after = (Get-FileHash -Algorithm SHA256 -LiteralPath $rel).Hash.ToLowerInvariant()
 if ($before -ne $after) { throw 'Actual applied source changed' }
 $inventory += [ordered]@{path=$rel;source_sha256_before=$before;source_sha256_after=$after;source_copy=$source;candidate_copy=$candidate;candidate_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $candidate).Hash.ToLowerInvariant()}
}
[System.IO.File]::WriteAllText((Join-Path $repo $patchRel), $patch, $utf8)
$checkOutput = & git apply --check -- $patchRel 2>&1
$checkExit = $LASTEXITCODE
[System.IO.File]::WriteAllText((Join-Path $repo $logRel), ("command: git apply --check -- $patchRel`nexit: $checkExit`n" + (($checkOutput | ForEach-Object { $_.ToString() }) -join "`n") + "`n"), $utf8)
$artifacts = foreach ($rel in @($patchRel, $logRel, 'scratch/performance/prepare-inherited-slot-proof-r3-incremental-20261008.ps1', 'scratch/performance/inherited-slot-proof-displaced-CPP-20261008.h')) {
 [ordered]@{path=$rel;bytes=(Get-Item -LiteralPath $rel).Length;sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $rel).Hash.ToLowerInvariant()}
}
$record = [ordered]@{status='scratch_only_r3_incremental_not_applied_not_built_not_run';head=$head;based_on='Applied inherited R2; build 77574 exit0, no runtime/measurement';parent_r2_patch_sha256='1452f7291f98998e765ab764010e8a5f0fc7f6038ee30b5fc5a090ee63e53e71';apply_check_exit=$checkExit;targets=@($inventory);artifacts=@($artifacts)}
[System.IO.File]::WriteAllText((Join-Path $repo $provenanceRel), (($record | ConvertTo-Json -Depth 6) + "`n"), $utf8)
if ($checkExit -ne 0) { throw 'Incremental patch not mechanically applicable' }
Write-Output 'R3 displaced-index guard/test patch checked; two actual applied R2 files unchanged.'
