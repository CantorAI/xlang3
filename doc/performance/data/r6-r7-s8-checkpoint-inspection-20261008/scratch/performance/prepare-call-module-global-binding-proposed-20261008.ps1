$ErrorActionPreference='Stop'
$taskRoot='D:/CantorAI/xlang3'
$taskRel='src/executor/xlang_vm/ops/xlang_vm_ops_call.h'
$taskStem=Join-Path $taskRoot 'scratch/performance/call-module-global-binding-proposed-20261008'
$taskBytes=[IO.File]::ReadAllBytes((Join-Path $taskRoot $taskRel))
$taskRawHash=(Get-FileHash -LiteralPath (Join-Path $taskRoot $taskRel) -Algorithm SHA256).Hash.ToLowerInvariant()
if ($taskRawHash -ne 'a84f771ff95698f27c5536bb8ce81d5de6f5c21c80c705f3933b5c13a2c35069') { throw 'C5 CallModuleMethod source drift'; }
$taskOriginal=[Text.Encoding]::UTF8.GetString($taskBytes).Replace("`r`n","`n")
$taskNeedle=@"
  uint32_t resolved_module_slot = 0;
  std::string module_slot_error;
  if (!module_find_attr_slot(globals_module, module.global_slots[in.a], resolved_module_slot, module_slot_error) ||
      resolved_module_slot >= globals_module_obj->slots.size()) {
    return raise_runtime_error("module slot is not bound") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const auto& module_value = globals_module_obj->slots[resolved_module_slot];
"@
$taskReplacement=@"
  uint32_t resolved_module_slot = 0;
  auto& global_cache = instr_cache[ip].global;
  // in.a names a compiled global, not the live Module's physical slot. Reuse
  // only that binding's index under the same mutation guard as LoadModuleSlot;
  // always reread its live Value before the existing export/call dispatch.
  // The frame owns globals_module throughout this activation, and frame pop
  // clears the global payload even when a scalar CallMethod proof survives.
  // Do not retain a receiver/callee or carry this local-version proof across
  // activations: Module versions alone do not prevent pointer-reuse ABA.
  if (global_cache.kind == 1 && global_cache.version == globals_module_obj->version &&
      global_cache.slot < globals_module_obj->slots.size() &&
      globals_module_obj->slots[global_cache.slot].tag != ValueTag::Invalid) {
    resolved_module_slot = global_cache.slot;
  } else {
    std::string module_slot_error;
    if (!module_find_attr_slot(globals_module, module.global_slots[in.a], resolved_module_slot, module_slot_error) ||
        resolved_module_slot >= globals_module_obj->slots.size()) {
      return raise_runtime_error("module slot is not bound") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    // Publish scalar binding metadata before any export/property/call callback.
    global_cache.kind = 1;
    global_cache.slot = resolved_module_slot;
    global_cache.version = globals_module_obj->version;
  }
  const auto& module_value = globals_module_obj->slots[resolved_module_slot];
"@
if (($taskOriginal.Split([string[]]@($taskNeedle),[StringSplitOptions]::None).Count-1) -ne 1) { throw 'Binding anchor is not unique'; }
$taskCandidate=$taskOriginal.Replace($taskNeedle,$taskReplacement)
$taskInputs=$taskStem+'-inputs';$taskCandidates=$taskStem+'-candidates'
foreach ($taskNew in @($taskInputs,$taskCandidates,$taskStem+'.patch',$taskStem+'-provenance.json')) { if (Test-Path -LiteralPath $taskNew) { throw 'Refuse artifact overwrite'; } }
New-Item -ItemType Directory -Force -Path (Split-Path (Join-Path $taskInputs $taskRel)),(Split-Path (Join-Path $taskCandidates $taskRel)) | Out-Null
[IO.File]::WriteAllBytes((Join-Path $taskInputs $taskRel),$taskBytes)
[IO.File]::WriteAllText((Join-Path $taskCandidates $taskRel),$taskCandidate,[Text.UTF8Encoding]::new($false))
$taskDiff=& git diff --no-index --ignore-space-at-eol -- (Join-Path $taskInputs $taskRel) (Join-Path $taskCandidates $taskRel) 2>$null
if ($LASTEXITCODE -ne 1) { throw 'Unexpected Git diff result'; }
$taskPatch=($taskDiff -join "`n")+"`n"
$taskPatch=[regex]::Replace($taskPatch,'(?m)^diff --git .*$','diff --git a/'+$taskRel+' b/'+$taskRel)
$taskPatch=[regex]::Replace($taskPatch,'(?m)^--- .*$','--- a/'+$taskRel)
$taskPatch=[regex]::Replace($taskPatch,'(?m)^\+\+\+ .*$','+++ b/'+$taskRel)
[IO.File]::WriteAllText($taskStem+'.patch',$taskPatch,[Text.UTF8Encoding]::new($false))
& git apply --check ($taskStem+'.patch')
$taskExact=$LASTEXITCODE
if ($taskExact -ne 0) {
  & git apply --check --ignore-whitespace ($taskStem+'.patch')
  if ($LASTEXITCODE -ne 0) { throw 'Apply check failed'; }
}
$taskInventory=Join-Path $taskRoot 'doc/performance/data/class-constructor-plan-c5-applied-source-20261008.json'
if ((Get-FileHash -LiteralPath $taskInventory -Algorithm SHA256).Hash.ToLowerInvariant() -ne '69aaf03539202bbc7fced4dfe46df5f3a801a6d40b8e0360a68245de6ce71937') { throw 'C5 inventory drift'; }
$taskPins=Get-Content -Raw -LiteralPath $taskInventory|ConvertFrom-Json
foreach ($taskPin in $taskPins.source_sha256.psobject.Properties) {
  if ((Get-FileHash -LiteralPath (Join-Path $taskRoot $taskPin.Name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskPin.Value) { throw "Source drift: $($taskPin.Name)"; }
}
$taskProof=[ordered]@{
  status='held_scratch_only_uncompiled_unexecuted_activation_global_binding_proposal'
  source_inventory=$taskInventory.Replace('\','/')
  source_inventory_sha256='69aaf03539202bbc7fced4dfe46df5f3a801a6d40b8e0360a68245de6ce71937'
  source_count=@($taskPins.source_sha256.psobject.Properties).Count
  raw_before_sha256=@{$taskRel=$taskRawHash}
  candidate_root=$taskCandidates.Replace('\','/');input_root=$taskInputs.Replace('\','/')
  mapping=@{$taskRel=$taskRel}
  candidate_source_sha256=@{$taskRel=(Get-FileHash -LiteralPath (Join-Path $taskCandidates $taskRel) -Algorithm SHA256).Hash.ToLowerInvariant()}
  patch_sha256=(Get-FileHash -LiteralPath ($taskStem+'.patch') -Algorithm SHA256).Hash.ToLowerInvariant()
  preparer_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
  held_note='scratch/performance/call-module-global-slot-binding-source-note-20261008.md'
  held_note_sha256=(Get-FileHash -LiteralPath (Join-Path $taskRoot 'scratch/performance/call-module-global-slot-binding-source-note-20261008.md') -Algorithm SHA256).Hash.ToLowerInvariant()
  git_apply_exact_check_exit=$taskExact;git_apply_check_ignore_whitespace_exit=0
  all_actual_source_inputs_unchanged=$true
  compiler_runtime_ast_benchmark_executed=$false
  new_cache_fields_or_owners=$false;cross_activation_global_persistence=$false
  scope='Use existing per-site global kind1/index/version; same LoadModuleSlot validity checks; current live slot value only. Publish before callbacks. Existing CallMethod domain/pop global clearing and native/export/property/nonmodule/error/observer dispatch unchanged.'
  measured_gain=$null
}
[IO.File]::WriteAllText($taskStem+'-provenance.json',($taskProof|ConvertTo-Json -Depth 8)+"`n",[Text.UTF8Encoding]::new($false))
[ordered]@{patch=$taskStem+'.patch';patch_sha256=$taskProof.patch_sha256;provenance=$taskStem+'-provenance.json';provenance_sha256=(Get-FileHash -LiteralPath ($taskStem+'-provenance.json') -Algorithm SHA256).Hash.ToLowerInvariant();candidate_source_sha256=$taskProof.candidate_source_sha256;exact_apply_check_exit=$taskExact}|ConvertTo-Json -Depth 8