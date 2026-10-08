$ErrorActionPreference = 'Stop'
$taskRoot = 'D:/CantorAI/xlang3'
$taskRel = 'src/runtime/runtime.cpp'
$taskStem = Join-Path $taskRoot 'scratch/performance/published-frame-same-owner-refresh-proposed-20261008'
$taskSource = Join-Path $taskRoot $taskRel
$taskBytes = [IO.File]::ReadAllBytes($taskSource)
$taskRawHash = (Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash.ToLowerInvariant()
if ($taskRawHash -ne '67b6383867234d092f479512feb80da9387b3a8c5280701ae87cca77947a9af9') { throw 'Runtime source drift'; }
$taskInventory = Join-Path $taskRoot 'doc/performance/data/class-constructor-plan-c5-applied-source-20261008.json'
$taskInventoryHash = (Get-FileHash -LiteralPath $taskInventory -Algorithm SHA256).Hash.ToLowerInvariant()
if ($taskInventoryHash -ne '69aaf03539202bbc7fced4dfe46df5f3a801a6d40b8e0360a68245de6ce71937') { throw 'Inventory drift'; }
$taskInventoryData = Get-Content -Raw -LiteralPath $taskInventory | ConvertFrom-Json
foreach ($taskPin in $taskInventoryData.source_sha256.psobject.Properties) {
  if ((Get-FileHash -LiteralPath (Join-Path $taskRoot $taskPin.Name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskPin.Value) { throw "Source input drift: $($taskPin.Name)"; }
}
$taskOriginal = [Text.Encoding]::UTF8.GetString($taskBytes).Replace("`r`n", "`n")
$taskNeedle = @"
  auto& published = g_runtime_frame_registry[&runtime][runtime_current_thread_ident()];
  published.state = source;
  published.owned_frames.clear();
"@
$taskReplacement = @"
  auto& published = g_runtime_frame_registry[&runtime][runtime_current_thread_ident()];
  // Native suspension repeatedly publishes stacks with the same live owners.
  // Retain the existing snapshot only after a complete identity proof: its
  // Module/global owners keep every refreshed view valid without rebuilding
  // owning records. Count/owner/null changes keep the original cold path and
  // its retirement order; arbitrary local objects are never retained here.
  const auto same_value_owner = [](const Value& old_value, const Value& new_value) {
    if (old_value.tag != new_value.tag || old_value.flags != new_value.flags ||
        (old_value.flags & kXlangValueBorrowedRefFlag) != 0) return false;
    if (old_value.tag == ValueTag::Object)
      return old_value.as.obj != nullptr && old_value.as.obj == new_value.as.obj;
    return old_value.flags == 0 &&
        (old_value.tag == ValueTag::Invalid || old_value.tag == ValueTag::None);
  };
  bool same_owners = source.frame_stack != nullptr && source.frame_stack_count != 0 &&
      published.owned_frames.size() == source.frame_stack_count &&
      published.frame_stack.size() == source.frame_stack_count &&
      same_value_owner(published.state.current_globals_module, source.current_globals_module) &&
      same_value_owner(published.state.trace_function, source.trace_function) &&
      same_value_owner(published.state.profile_function, source.profile_function);
  for (size_t i = 0; same_owners && i < source.frame_stack_count; ++i) {
    const auto& frame = source.frame_stack[i];
    const auto& owned = published.owned_frames[i];
    same_owners = frame.module_owner != nullptr && frame.module_owner->get() != nullptr &&
        owned.module_owner.get() == frame.module_owner->get() &&
        !owned.module_owner.owner_before(*frame.module_owner) &&
        !frame.module_owner->owner_before(owned.module_owner) &&
        frame.globals_module != nullptr && frame.instruction_index != nullptr &&
        same_value_owner(owned.globals_module, *frame.globals_module) &&
        owned.local_values.empty();
  }
  if (same_owners) {
    published.state = source;
    for (size_t i = 0; i < source.frame_stack_count; ++i) {
      const auto& frame = source.frame_stack[i];
      auto& owned = published.owned_frames[i];
      owned.instruction_index = *frame.instruction_index;
      owned.function_id = frame.function_id;
      owned.activation_id = frame.activation_id;
      const std::vector<std::string>* local_names = nullptr;
      if (owned.function_id < owned.module_owner->functions.size())
        local_names = &owned.module_owner->functions[owned.function_id].locals;
      published.frame_stack[i] = RuntimeFrameView{
          &owned.module_owner,
          &owned.globals_module,
          local_names,
          owned.local_values.data(),
          &owned.instruction_index,
          owned.local_values.size(),
          owned.function_id,
          owned.activation_id,
          nullptr,
          nullptr,
          0,
          nullptr,
      };
    }
    published.state.frame_stack = published.frame_stack.data();
    published.state.frame_stack_count = published.frame_stack.size();
    return;
  }
  published.state = source;
  published.owned_frames.clear();
"@
if (($taskOriginal.Split([string[]]@($taskNeedle), [StringSplitOptions]::None).Count - 1) -ne 1) { throw 'Publisher anchor not unique'; }
$taskCandidate = $taskOriginal.Replace($taskNeedle, $taskReplacement)
$taskInputRoot = $taskStem + '-inputs'
$taskCandidateRoot = $taskStem + '-candidates'
$taskPatchPath = $taskStem + '.patch'
$taskProofPath = $taskStem + '-provenance.json'
foreach ($taskNewPath in @($taskInputRoot,$taskCandidateRoot,$taskPatchPath,$taskProofPath)) { if (Test-Path -LiteralPath $taskNewPath) { throw "Refuse to overwrite artifact: $taskNewPath"; } }
New-Item -ItemType Directory -Force -Path (Split-Path (Join-Path $taskInputRoot $taskRel)),(Split-Path (Join-Path $taskCandidateRoot $taskRel)) | Out-Null
[IO.File]::WriteAllBytes((Join-Path $taskInputRoot $taskRel), $taskBytes)
[IO.File]::WriteAllText((Join-Path $taskCandidateRoot $taskRel), $taskCandidate, [Text.UTF8Encoding]::new($false))
$taskCandidateHash = (Get-FileHash -LiteralPath (Join-Path $taskCandidateRoot $taskRel) -Algorithm SHA256).Hash.ToLowerInvariant()
$taskDiff = & git diff --no-index --ignore-space-at-eol -- (Join-Path $taskInputRoot $taskRel) (Join-Path $taskCandidateRoot $taskRel) 2>$null
if ($LASTEXITCODE -ne 1) { throw 'Unexpected Git diff result'; }
$taskPatch = ($taskDiff -join "`n") + "`n"
$taskPatch = [regex]::Replace($taskPatch, '(?m)^diff --git .*$', 'diff --git a/' + $taskRel + ' b/' + $taskRel)
$taskPatch = [regex]::Replace($taskPatch, '(?m)^--- .*$', '--- a/' + $taskRel)
$taskPatch = [regex]::Replace($taskPatch, '(?m)^\+\+\+ .*$', '+++ b/' + $taskRel)
[IO.File]::WriteAllText($taskPatchPath, $taskPatch, [Text.UTF8Encoding]::new($false))
& git apply --check $taskPatchPath
if ($LASTEXITCODE -ne 0) { throw 'Exact apply check failed'; }
foreach ($taskPin in $taskInventoryData.source_sha256.psobject.Properties) {
  if ((Get-FileHash -LiteralPath (Join-Path $taskRoot $taskPin.Name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskPin.Value) { throw "Actual source changed: $($taskPin.Name)"; }
}
$taskProof = [ordered]@{
  status = 'held_scratch_only_uncompiled_unexecuted_snapshot_refresh_proposal'
  source_inventory = $taskInventory.Replace('\','/')
  source_inventory_sha256 = $taskInventoryHash
  source_count = @($taskInventoryData.source_sha256.psobject.Properties).Count
  raw_before_sha256 = @{ $taskRel = $taskRawHash }
  candidate_source_sha256 = @{ $taskRel = $taskCandidateHash }
  candidate_root = $taskCandidateRoot.Replace('\','/')
  input_root = $taskInputRoot.Replace('\','/')
  mapping = @{ $taskRel = $taskRel }
  patch_sha256 = (Get-FileHash -LiteralPath $taskPatchPath -Algorithm SHA256).Hash.ToLowerInvariant()
  preparer_sha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
  exact_git_apply_check_exit = 0
  all_actual_source_inputs_unchanged = $true
  compiler_runtime_ast_benchmark_executed = $false
  evidence_attribution = 'doc/performance/data/pickle-original-pure-c5-native-coff-attribution-20261008.json'
  evidence_attribution_sha256 = 'f13ca5768fc3a55a581ea8a70af924b584666a6b4d9793e750436c58ed5eceab'
  design = 'scratch/performance/published-frame-same-owner-refresh-design-20261008.md'
  design_sha256 = '524827794db6d7c0a28c12501e8a851d41e40e65155cfe68cebda162db507d32'
  measured_gain = $null
  scope = 'Same nonzero count and exact live existing owner proof before scalar/view-only updates. Full fallback, mutex, suspension schedule, local-owner exclusion and finalizer retirement order unchanged.'
}
[IO.File]::WriteAllText($taskProofPath, ($taskProof | ConvertTo-Json -Depth 8) + "`n", [Text.UTF8Encoding]::new($false))
[ordered]@{
  patch = $taskPatchPath.Replace('\','/'); patch_sha256 = $taskProof.patch_sha256
  provenance = $taskProofPath.Replace('\','/'); provenance_sha256 = (Get-FileHash -LiteralPath $taskProofPath -Algorithm SHA256).Hash.ToLowerInvariant()
  candidate_root = $taskProof.candidate_root; candidate_source_sha256 = $taskProof.candidate_source_sha256
} | ConvertTo-Json -Depth 8