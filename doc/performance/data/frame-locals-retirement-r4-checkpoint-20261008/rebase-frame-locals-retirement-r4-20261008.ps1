$ErrorActionPreference = 'Stop'
$retireRebaseRoot = 'D:/CantorAI/xlang3'
Set-Location -LiteralPath $retireRebaseRoot
$retireRebaseTarget = 'scratch/performance/freeze-frame-locals-retirement-r4-proposed-20261008.ps1'
if (Test-Path -LiteralPath $retireRebaseTarget) { throw 'Preserve frozen generator' }
$retireRebaseSource = [System.IO.File]::ReadAllText((Join-Path $retireRebaseRoot 'scratch/performance/freeze-frame-locals-retirement-r3-proposed-20261008.ps1')).Replace("`r`n", "`n")
$retireRebaseSource = $retireRebaseSource.Replace('frame-locals-retirement-r3-proposed-20261008', 'frame-locals-retirement-r4-proposed-20261008')
$retireRebaseSource = $retireRebaseSource.Replace('doc/performance/data/callee-module-owner-r10-applied-source-20261008.json', 'doc/performance/data/sorted-exact-int-s8-registered-source-20261008.json')
$retireRebaseSource = $retireRebaseSource.Replace('178268da8f50e5e0ad3640f68ffa48e2e2193903495981c540642d27486628c1', 'f20c304e32b0874c929d22dc72018cbd6d5fd9d124844c1d66d8edcc6a231556')
$retireRebaseSource = $retireRebaseSource.Replace('Wrong current R10 inventory', 'Wrong restored S8 inventory').Replace('Expected current111', 'Expected restored110').Replace('$retireBefore.Count -ne 111', '$retireBefore.Count -ne 110').Replace('parent_source_count = 111', 'parent_source_count = 110')
$retireRebaseSource = $retireRebaseSource.Replace('#include "callee_module_owner_cases.h"', '#include "class_method_annotation_capture_cases.h"').Replace('callee_module_owner_cases.h', 'class_method_annotation_capture_cases.h').Replace('check_callee_module_owner_selection', 'check_class_method_annotation_capture')
$retireRebaseSource = $retireRebaseSource.Replace('CPP main retains R10 additive include/check.', 'CPP main adds only the retirement include/check to the restored S8 registrations; withdrawn R10 is absent.')
$retireRebaseSource = $retireRebaseSource.Replace("prior_held_patch_sha256 = 'ab8497d847d26ba649434aa737b82ff6da5943f01f077777f44201b6bf797b53'", "prior_held_patch_sha256 = '52222af65665b1e133523ce7a2cdd618fb31bb1cdaf0c7af0567070a8429bcf9'")
$retireRebaseGuard = @'
$retireWithdrawal = 'doc/performance/data/callee-module-owner-r10-withdrawn-unmeasured-duplicate-restored-s8-20261008.json'
$retireWithdrawalHash = '876cac9e56405c9566aa29a7419d4a6acf2cc9ab6f83f6d3190ea6a4d38f25eb'
if ((Retire-Hash $retireWithdrawal) -ne $retireWithdrawalHash) { throw 'Wrong actual withdrawal receipt' }
$retireWithdrawalRecord = Get-Content -LiteralPath $retireWithdrawal -Raw | ConvertFrom-Json
if (!$retireWithdrawalRecord.terminal -or $retireWithdrawalRecord.status -ne 'withdrawn_unmeasured_duplicate_r10_preserved_exact_s8_restored' -or $retireWithdrawalRecord.withdrawal.r10_timed_children -ne 0) { throw 'Require actual exact S8 restoration, no R10 timing' }
'@
$retireRebaseSource = $retireRebaseSource.Replace("`$retireParent = Get-Content", $retireRebaseGuard + "`n`$retireParent = Get-Content")
$retireRebaseFields = @'
  restoration_receipt = $retireWithdrawal
  restoration_receipt_sha256 = $retireWithdrawalHash
  restored_parent_control_manifest_sha256 = '67449b0b9ecd5a1b4c9669acef85d8b89eb28c7b31df60b7a4653ad5a7c79b56'
  withdrawn_r10_control_manifest_sha256 = '27a44d1a57c2832dd053def0ebd6eb1114fd797ab5e33484fe21d14692133427'
  rebase_scope = 'Only CPP registration and raw parent refresh from withdrawn R10 to actually restored S8. Runtime candidate and public CPP header must remain exact reviewed R3 bytes.'
'@
$retireRebaseSource = $retireRebaseSource.Replace('  parent_source_count = 110', '  parent_source_count = 110' + "`n" + $retireRebaseFields)
$retireRebaseSource = $retireRebaseSource.Replace('Preserve current R10/S8 controls;', 'Preserve restored S8 and withdrawn unmeasured R10 controls;')
[System.IO.File]::WriteAllText((Join-Path $retireRebaseRoot $retireRebaseTarget), $retireRebaseSource, [System.Text.UTF8Encoding]::new($false))
& (Join-Path $retireRebaseRoot $retireRebaseTarget)
$retireR4 = Get-Content -LiteralPath 'scratch/performance/frame-locals-retirement-r4-proposed-20261008-provenance.json' -Raw | ConvertFrom-Json
if ($retireR4.candidate_source_sha256.'src/runtime/runtime.cpp' -ne '2d7a0836eea66c376f652276d35edaa412edf792ab74c220711cd4bd3a14ca98' -or $retireR4.candidate_source_sha256.'tests/cpp/frame_locals_retirement_cases.h' -ne '1449d30c46298ed0d63d555ca5c5b826f39ec02586a9cbba11800c762e946eda') { throw 'Runtime/CPP semantic bytes changed during registration rebase' }
