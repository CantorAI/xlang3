$ErrorActionPreference='Stop'
$taskRoot='D:/CantorAI/xlang3'
$taskStem=Join-Path $taskRoot 'scratch/performance/published-frame-snapshot-tests-proposed-20261008'
$taskMain='tests/cpp/interpreter_tests.cpp'
$taskHeader='tests/cpp/published_frame_snapshot_cases.h'
$taskFragment=Join-Path $taskRoot 'scratch/performance/published-frame-snapshot-cpp-proposed-20261008.h'
$taskFragmentHash=(Get-FileHash -LiteralPath $taskFragment -Algorithm SHA256).Hash.ToLowerInvariant()
if ($taskFragmentHash -ne '6bccd0d990bb438cc92d2de029bf323c2f1ff48ca36a13e3d72723261b7b4e83') { throw 'CPP fragment drift'; }
if (Test-Path -LiteralPath (Join-Path $taskRoot $taskHeader)) { throw 'New header already exists'; }
$taskMainBytes=[IO.File]::ReadAllBytes((Join-Path $taskRoot $taskMain))
$taskMainHash=(Get-FileHash -LiteralPath (Join-Path $taskRoot $taskMain) -Algorithm SHA256).Hash.ToLowerInvariant()
if ($taskMainHash -ne '59bdb1c2e85079d28ad7359ebdc3b0e76c0c56bf3880e79c8da6384418b2d172') { throw 'CPP registration input drift'; }
$taskOriginal=[Text.Encoding]::UTF8.GetString($taskMainBytes).Replace("`r`n","`n")
$taskInclude='#include "class_method_annotation_capture_cases.h"'
$taskCall='  xlang3::test::check_class_method_annotation_capture(result);'
foreach ($taskAnchor in @($taskInclude,$taskCall)) {
  if (($taskOriginal.Split([string[]]@($taskAnchor),[StringSplitOptions]::None).Count - 1) -ne 1) { throw 'Registration anchor not unique'; }
}
$taskCandidate=$taskOriginal.Replace($taskInclude,$taskInclude+"`n"+'#include "published_frame_snapshot_cases.h"').Replace($taskCall,$taskCall+"`n"+'  xlang3::test::check_published_frame_same_owner_refresh(result);')
$taskInputs=$taskStem+'-inputs'
$taskCandidates=$taskStem+'-candidates'
$taskPatchPath=$taskStem+'.patch'
$taskProofPath=$taskStem+'-provenance.json'
foreach ($taskNew in @($taskInputs,$taskCandidates,$taskPatchPath,$taskProofPath)) { if (Test-Path -LiteralPath $taskNew) { throw 'Refuse to overwrite test artifact'; } }
New-Item -ItemType Directory -Force -Path (Split-Path (Join-Path $taskInputs $taskMain)),(Split-Path (Join-Path $taskCandidates $taskMain)) | Out-Null
[IO.File]::WriteAllBytes((Join-Path $taskInputs $taskMain),$taskMainBytes)
[IO.File]::WriteAllText((Join-Path $taskCandidates $taskMain),$taskCandidate,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllBytes((Join-Path $taskCandidates $taskHeader),[IO.File]::ReadAllBytes($taskFragment))
$taskDiff=& git diff --no-index --ignore-space-at-eol -- (Join-Path $taskInputs $taskMain) (Join-Path $taskCandidates $taskMain) 2>$null
if ($LASTEXITCODE -ne 1) { throw 'Unexpected registration diff result'; }
$taskPatch=($taskDiff -join "`n")+"`n"
$taskPatch=[regex]::Replace($taskPatch,'(?m)^diff --git .*$','diff --git a/'+$taskMain+' b/'+$taskMain)
$taskPatch=[regex]::Replace($taskPatch,'(?m)^--- .*$','--- a/'+$taskMain)
$taskPatch=[regex]::Replace($taskPatch,'(?m)^\+\+\+ .*$','+++ b/'+$taskMain)
$taskHeaderText=[IO.File]::ReadAllText($taskFragment)
if (-not $taskHeaderText.EndsWith("`n")) { throw 'Header lacks final newline'; }
$taskLines=$taskHeaderText.Substring(0,$taskHeaderText.Length-1).Split("`n")
$taskPatch+="diff --git a/$taskHeader b/$taskHeader`nnew file mode 100644`n--- /dev/null`n+++ b/$taskHeader`n@@ -0,0 +1,$($taskLines.Count) @@`n"
$taskPatch+=(($taskLines | ForEach-Object { '+'+$_ }) -join "`n")+"`n"
[IO.File]::WriteAllText($taskPatchPath,$taskPatch,[Text.UTF8Encoding]::new($false))
& git apply --check $taskPatchPath
$taskExact=$LASTEXITCODE
if ($taskExact -ne 0) {
  & git apply --check --ignore-whitespace $taskPatchPath
  if ($LASTEXITCODE -ne 0) { throw 'Read-only apply check failed'; }
}
$taskInventory=Join-Path $taskRoot 'doc/performance/data/class-constructor-plan-c5-applied-source-20261008.json'
$taskInventoryHash=(Get-FileHash -LiteralPath $taskInventory -Algorithm SHA256).Hash.ToLowerInvariant()
if ($taskInventoryHash -ne '69aaf03539202bbc7fced4dfe46df5f3a801a6d40b8e0360a68245de6ce71937') { throw 'Inventory drift'; }
$taskInventoryData=Get-Content -Raw -LiteralPath $taskInventory | ConvertFrom-Json
foreach ($taskPin in $taskInventoryData.source_sha256.psobject.Properties) {
  if ((Get-FileHash -LiteralPath (Join-Path $taskRoot $taskPin.Name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskPin.Value) { throw "Actual source changed: $($taskPin.Name)"; }
}
$taskCandidateHashes=[ordered]@{}
foreach ($taskRel in @($taskMain,$taskHeader)) {
  $taskCandidateHashes[$taskRel]=(Get-FileHash -LiteralPath (Join-Path $taskCandidates $taskRel) -Algorithm SHA256).Hash.ToLowerInvariant()
}
$taskProof=[ordered]@{
  status='held_scratch_only_uncompiled_unexecuted_public_cpp_snapshot_tests'
  source_inventory=$taskInventory.Replace('\','/')
  source_inventory_sha256=$taskInventoryHash
  source_count=@($taskInventoryData.source_sha256.psobject.Properties).Count
  raw_before_sha256=@{$taskMain=$taskMainHash}
  required_absent_targets=@($taskHeader)
  candidate_root=$taskCandidates.Replace('\','/')
  input_root=$taskInputs.Replace('\','/')
  mapping=@{$taskMain=$taskMain;$taskHeader=$taskHeader}
  candidate_source_sha256=$taskCandidateHashes
  patch_sha256=(Get-FileHash -LiteralPath $taskPatchPath -Algorithm SHA256).Hash.ToLowerInvariant()
  cpp_fragment_sha256=$taskFragmentHash
  preparer_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
  engine_patch='scratch/performance/published-frame-same-owner-refresh-proposed-20261008.patch'
  engine_patch_sha256='bd18ffd8e9c41c10a43110a531c9db24ec9503ab32a54a102dd4b239bf121429'
  engine_provenance_sha256='ab41aa60a3e33963f8b1e1525631cc3c4ded1a83a4f601faeb0819dfa44972e6'
  git_apply_exact_check_exit=$taskExact
  git_apply_check_ignore_whitespace_exit=0
  all_actual_source_inputs_unchanged=$true
  compiler_runtime_ast_benchmark_executed=$false
  eligibility='Actual exported Runtime publisher on joined worker; observer excluded from counter brackets. Cold Module Value retains >0;32 same-owner scalar refresh publications exactly0 Module Value retains/releases; distinct safe shared_ptr control-block positive coldretain/release and once-retirement audit. Counter enabled restored without reset or new instrumentation.'
  correctness='Actual observer FrameObject Module/function/IP/activation/emptylocals/chain;changedglobals and Module owner;grow/shrink/null/clear fallback;profile/trace/local/global/control-block exactlyonce lifetime audits. Every context owns shared state; joined worker precedes Runtime destruction.'
  measured_gain=$null
}
[IO.File]::WriteAllText($taskProofPath,($taskProof|ConvertTo-Json -Depth 8)+"`n",[Text.UTF8Encoding]::new($false))
[ordered]@{patch=$taskPatchPath.Replace('\','/');patch_sha256=$taskProof.patch_sha256;provenance=$taskProofPath.Replace('\','/');provenance_sha256=(Get-FileHash -LiteralPath $taskProofPath -Algorithm SHA256).Hash.ToLowerInvariant();candidate_root=$taskProof.candidate_root;candidate_source_sha256=$taskCandidateHashes}|ConvertTo-Json -Depth 8