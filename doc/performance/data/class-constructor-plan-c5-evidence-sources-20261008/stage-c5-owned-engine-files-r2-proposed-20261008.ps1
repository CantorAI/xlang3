param(
  [Parameter(Mandatory=$true)][string]$ValidationPath,
  [Parameter(Mandatory=$true)][string]$ValidationSha256,
  [Parameter(Mandatory=$true)][string]$ReceiptPath,
  [switch]$StageOwned
)
$ErrorActionPreference = 'Stop'
$repo = 'D:/CantorAI/xlang3'
$manifestPath = 'scratch/performance/c5-owned-commit-follow-up-20261008-manifest.json'
$manifestHash = '9197e03b70088fe0149bc04903f991b6207172f3d7ad06c5e0fc6e8e4218dda0'
$patchHash = '3cddd11287c9eff18aea194f709dea0c32dcdb4419033c82f250ddf4665e72ed'
$inventoryHash = '69aaf03539202bbc7fced4dfe46df5f3a801a6d40b8e0360a68245de6ce71937'
$dependencyPath = 'scratch/performance/c5-owned-engine-excluded-dependency-audit-20261008.json'
$dependencyHash = '65748b04a8fb9f4b163e2c16afa4bfe45d24da1fce2bef0c36ef55ce7da8f216'
$validationController = 'scratch/performance/validate-ordinary-canonical-slot-constructor-c5-gate-idle-resume-20261008.py'
$validationControllerHash = 'd598c2c271ebe461416e0a8b794b9ed471ce433b33d2dc7d0c40965084b18a25'
$priorController = 'scratch/performance/validate-ordinary-canonical-slot-constructor-c5-full-20261008.py'
$priorControllerHash = 'fa926e9d5e0e7e8d7ea9a8e3377d88ee7ce4b708928902ae16f20f00c5311201'
$priorReceipt = 'doc/performance/data/ordinary-canonical-slot-constructor-c5-full-validation-20261008.json'
$priorReceiptHash = '68d713e4c3758436e7be662cd5368c74cbb9b9057972c16ab1c2354b4299701b'
$focusedReceiptHash = '96e5db977faeca71a96a48be799dba661b8695ca20a0b80c2bd33258187f2aa5'
$freshGateHash = '9c9774b84402f95ff4b76a09e93ec3ed39666c0caae64474759e84050d7b5e4c'
$reusedNames = @(
  'focused-call-ex-cross-activation-constructor','focused-call-method-dict-cache-touch',
  'focused-inherited-call-ex-constructor','focused-native-bound-zero-args',
  'focused-sqlite-native-aggregate','focused-sqlite-cursor-completion',
  'focused-identity-last-use','focused-hash-exception-preservation',
  'focused-sqlite-statement-cache','focused-exact-string-hash',
  'focused-cursor-writer-guards','ctest-inventory','cpp-native-sqlite',
  'manual-old-xlang-sqlite-api','manual-python-sqlite3-api','full-fixtures-core-compat-expected'
)
$utf8 = [Text.UTF8Encoding]::new($false)
function FileHash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function GitResult([string]$arguments) {
  $start = [Diagnostics.ProcessStartInfo]::new('git', $arguments)
  $start.WorkingDirectory = $repo
  $start.UseShellExecute = $false
  $start.RedirectStandardOutput = $true
  $start.RedirectStandardError = $true
  $p = [Diagnostics.Process]::Start($start)
  $out = $p.StandardOutput.ReadToEnd()
  $err = $p.StandardError.ReadToEnd()
  $p.WaitForExit()
  [pscustomobject]@{output=$out;error=$err;exit_code=$p.ExitCode}
}
function SameMap($left,$right) {
  if (@($left.PSObject.Properties).Count -ne @($right.PSObject.Properties).Count) {return $false}
  foreach($p in $left.PSObject.Properties) {
    if ($null -eq $right.PSObject.Properties[$p.Name] -or $right.PSObject.Properties[$p.Name].Value -ne $p.Value) {return $false}
  }
  return $true
}
function AssertIdle {
  $busy = @(Get-CimInstance Win32_Process | Where-Object {
    $_.ProcessId -ne $PID -and $_.Name -match '^(python|pythonw|xlang3|xlang3_.+|cl|link|cmake|ctest|ninja|msbuild|vctip)\.exe$'
  })
  if($busy.Count -ne 0) {throw ('Host has active runtime/build/test process: '+(($busy|ForEach-Object {$_.Name+' PID'+$_.ProcessId}) -join ', '))}
}
if(-not [IO.Path]::IsPathRooted($ValidationPath)) {$ValidationPath=Join-Path $repo $ValidationPath}
if(-not [IO.Path]::IsPathRooted($ReceiptPath)) {$ReceiptPath=Join-Path $repo $ReceiptPath}
$ReceiptPath=[IO.Path]::GetFullPath($ReceiptPath)
if(-not $ReceiptPath.StartsWith($repo+'/',[StringComparison]::OrdinalIgnoreCase) -and
   -not $ReceiptPath.StartsWith($repo.Replace('/','\')+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Receipt must remain in the repository workspace'}
if(Test-Path -LiteralPath $ReceiptPath) {throw 'Refusing to overwrite a staging receipt'}
if($ValidationSha256 -notmatch '^[0-9a-fA-F]{64}$') {throw 'Supply the actual terminal validation SHA256'}
foreach($row in @(
  @($manifestPath,$manifestHash),@($dependencyPath,$dependencyHash),@($validationController,$validationControllerHash),
  @($priorController,$priorControllerHash),@($priorReceipt,$priorReceiptHash)
)) {if((FileHash (Join-Path $repo $row[0])) -ne $row[1]) {throw ('Frozen prerequisite drift: '+$row[0])}}
$manifest=Get-Content -LiteralPath (Join-Path $repo $manifestPath) -Raw|ConvertFrom-Json
$inventory=Get-Content -LiteralPath (Join-Path $repo $manifest.final_inventory) -Raw|ConvertFrom-Json
if((FileHash (Join-Path $repo $manifest.final_inventory)) -ne $inventoryHash -or $inventory.source_count -ne 106) {throw 'C5 source inventory drift'}
if((FileHash (Join-Path $repo $manifest.patch)) -ne $patchHash -or
   (FileHash (Join-Path $repo $manifest.nul_paths)) -ne $manifest.nul_paths_sha256) {throw 'Owned patch/path inventory drift'}
if((FileHash $ValidationPath) -ne $ValidationSha256.ToLowerInvariant()) {throw 'Terminal validation receipt drift'}
$validation=Get-Content -LiteralPath $ValidationPath -Raw|ConvertFrom-Json
if(-not $validation.terminal -or $validation.status -notin @('validated','correctness_and_gate_passed_official_failed') -or
   -not $validation.correctness_passed -or -not $validation.hashes_unchanged -or -not $validation.release_tree_unchanged -or
   $validation.source_inventory_sha256 -ne $inventoryHash -or
   $validation.terminal_record.controller_sha256 -ne $validationControllerHash -or
   -not (SameMap $validation.source_sha256 $inventory.source_sha256) -or
   -not (SameMap $validation.hashes_before $validation.hashes_after)) {throw 'Actual C5 correctness/gate validation must be terminal and source/binary stable'}
if(@($validation.phases).Count -eq 0 -or @($validation.phases|Where-Object {$_.name -notlike 'official-*' -and (-not $_.passed -or $_.exit_code -ne 0 -or $_.timeout)}).Count -ne 0) {throw 'Required correctness/gate phase is incomplete or failed'}
$prior=Get-Content -LiteralPath (Join-Path $repo $priorReceipt) -Raw|ConvertFrom-Json
$resume=$validation.same_candidate_correctness_resume
if(-not $prior.terminal -or $prior.status -ne 'failed_fixed-gate' -or -not $prior.correctness_passed -or
   -not $prior.hashes_unchanged -or -not $prior.release_tree_unchanged -or $prior.full_validated -or
   $prior.terminal_record.controller_sha256 -ne $priorControllerHash -or
   $prior.source_inventory_sha256 -ne $inventoryHash -or
   -not (SameMap $prior.source_sha256 $inventory.source_sha256) -or
   -not (SameMap $prior.binaries_sha256 $validation.binaries_sha256) -or
   -not (SameMap $prior.hashes_before $prior.hashes_after) -or
   $resume.sha256 -ne $priorReceiptHash -or $resume.original_controller_sha256 -ne $priorControllerHash -or
   $resume.original_status -ne 'failed_fixed-gate' -or $resume.passed_correctness_phase_count -ne 16 -or
   $validation.reused_correctness_phase_count -ne 16 -or
   @($prior.phases).Count -ne 17 -or @($validation.phases).Count -ne 18 -or
   ($resume.reusable_phases -join "`n") -cne ($reusedNames -join "`n")) {throw 'Exact prior C5 same-candidate correctness chain is required'}
if((FileHash $resume.receipt) -ne $priorReceiptHash) {throw 'Resume does not reference the frozen prior receipt'}
$oldGate=$prior.phases[-1]
if($oldGate.name -ne 'fixed-gate' -or $oldGate.passed -or $oldGate.exit_code -ne 2 -or $oldGate.timeout -or
   $prior.error -ne 'AssertionError: fixed-gate') {throw 'Historical inconclusive gate must remain failed and unreused'}
for($i=0;$i -lt 16;$i++) {
  $previous=$prior.phases[$i]
  $retained=$validation.phases[$i]
  if($previous.name -ne $reusedNames[$i] -or $retained.name -ne $previous.name -or
     -not $previous.passed -or $previous.exit_code -ne 0 -or $previous.timeout -or
     $retained.execution -ne 'reused_historical_same_candidate' -or
     $retained.resume_receipt_sha256 -ne $priorReceiptHash -or
     ($retained.command -join [char]0) -cne ($previous.command -join [char]0) -or
     $retained.timeout_seconds -ne $previous.timeout_seconds -or
     $retained.stdout_log -ne $previous.stdout_log -or $retained.stderr_log -ne $previous.stderr_log -or
     $retained.stdout_sha256 -ne $previous.stdout_sha256 -or $retained.stderr_sha256 -ne $previous.stderr_sha256) {throw ('Historical correctness phase mismatch: '+$reusedNames[$i])}
}
$focused=$validation.retained_current_targeted_proof
if($focused.sha256 -ne $focusedReceiptHash -or (FileHash $focused.path) -ne $focusedReceiptHash) {throw 'Current eleven-phase targeted proof drift'}
$focusedRecord=Get-Content -LiteralPath $focused.path -Raw|ConvertFrom-Json
if(-not $focusedRecord.terminal -or $focusedRecord.status -ne 'targeted_correctness_passed' -or -not $focusedRecord.hashes_unchanged -or
   $focusedRecord.source_inventory_sha256 -ne $inventoryHash -or
   -not (SameMap $focusedRecord.source_sha256 $inventory.source_sha256) -or
   -not (SameMap $focusedRecord.binaries_sha256 $validation.binaries_sha256) -or
   @($focusedRecord.phases).Count -ne 11 -or
   @($focusedRecord.phases|Where-Object {-not $_.passed -or $_.exit_code -ne 0 -or $_.timeout}).Count -ne 0) {throw 'Current C5 eleven-phase targeted correctness proof must match106/178'}
$gate=Join-Path ([IO.Path]::GetDirectoryName($ValidationPath)) $validation.fixed_gate.output
if((FileHash $gate) -ne $validation.fixed_gate.sha256 -or $validation.fixed_gate.sha256 -ne $freshGateHash -or
   $validation.fixed_gate.exit_code -ne 0 -or $validation.phases[16].name -ne 'fixed-gate' -or
   $validation.phases[16].execution -eq 'reused_historical_same_candidate') {throw 'Actual fresh passing gate receipt is required'}
$gateRecord=Get-Content -LiteralPath $gate -Raw|ConvertFrom-Json
if($gateRecord.status -ne 'pass' -or @($gateRecord.cases.PSObject.Properties).Count -ne 11 -or
   $gateRecord.repeats -ne 21 -or $gateRecord.warmup -ne 5 -or $gateRecord.threshold -ne 0.1) {throw 'Unchanged complete fixed gate is required'}
$official=$validation.official_pprint
$officialRows=@($validation.phases|Where-Object {$_.name -eq 'official-pprint'})
if($officialRows.Count -ne 1 -or $null -eq $official.exit_code -or $null -eq $officialRows[0].timeout -or
   $officialRows[0].exit_code -ne $official.exit_code) {throw 'Exactly one terminal original pprint attempt must be recorded'}
if($validation.full_validated -ne [bool]$official.complete -or
   ($validation.status -eq 'validated') -ne [bool]$official.complete) {throw 'Official completion/full-validation status is inconsistent'}
$officialPath=Join-Path ([IO.Path]::GetDirectoryName($ValidationPath)) $official.output
if($null -ne $official.sha256) {
  if((FileHash $officialPath) -ne $official.sha256) {throw 'Official pprint output drift'}
} elseif(Test-Path -LiteralPath $officialPath) {throw 'Unexpected unrecorded official output'}
foreach($p in $official.partial_evidence_sha256.PSObject.Properties) {
  if((FileHash (Join-Path ([IO.Path]::GetDirectoryName($ValidationPath)) $p.Name)) -ne $p.Value) {throw ('Official partial evidence drift: '+$p.Name)}
}
foreach($phase in $validation.phases) {
  foreach($stream in @('stdout','stderr')) {
    $name=$phase.PSObject.Properties[$stream+'_log'].Value
    $sha=$phase.PSObject.Properties[$stream+'_sha256'].Value
    if((FileHash (Join-Path ([IO.Path]::GetDirectoryName($ValidationPath)) $name)) -ne $sha) {throw ('Raw phase log drift: '+$phase.name)}
  }
}
if(@($validation.binaries_sha256.PSObject.Properties).Count -ne 178) {throw 'The complete current178 Release inventory is required'}
foreach($p in $validation.binaries_sha256.PSObject.Properties) {
  if((FileHash (Join-Path $repo $p.Name)) -ne $p.Value) {throw ('Validated Release file drift: '+$p.Name)}
}
foreach($p in $validation.hashes_before.PSObject.Properties) {
  if((FileHash $p.Name) -ne $p.Value) {throw ('Validated source/tool/dependency drift: '+$p.Name)}
}
function AssertSourcePins {
  foreach($p in $inventory.source_sha256.PSObject.Properties) {if((FileHash (Join-Path $repo $p.Name)) -ne $p.Value) {throw ('Compiled source drift: '+$p.Name)}}
  foreach($row in $manifest.owned_files) {
    if($row.path -notmatch '^(src|tests)/' -or $row.path.Contains('..')) {throw 'Unexpected owned target'}
    if((FileHash (Join-Path $repo $row.path)) -ne $row.compiled_raw_sha256 -or
       (FileHash (Join-Path $repo $row.stage_blob)) -ne $row.stage_blob_sha256) {throw ('Owned target/blob drift: '+$row.path)}
  }
}
AssertSourcePins
$head=(GitResult '--no-optional-locks rev-parse HEAD').output.Trim()
if($head -ne $manifest.head) {throw 'Expected HEAD7233 before owned staging'}
$empty=GitResult '--no-optional-locks diff --cached --quiet'
if($empty.exit_code -ne 0) {throw 'Index must be empty before exact owned staging'}
$indexRelative=(GitResult '--no-optional-locks rev-parse --git-path index').output.Trim()
$indexPath=if([IO.Path]::IsPathRooted($indexRelative)) {$indexRelative} else {Join-Path $repo $indexRelative}
$indexBefore=FileHash $indexPath
$excludedBefore=[ordered]@{}
foreach($row in $manifest.preserved_dirty_outside_union) {$excludedBefore[$row.path]=FileHash (Join-Path $repo $row.path)}
$check=GitResult ('--no-optional-locks apply --check --cached '+$manifest.patch)
if($check.exit_code -ne 0) {throw ('Owned index patch check failed: '+$check.error)}
if((FileHash $indexPath) -ne $indexBefore) {throw 'Read-only check changed index'}
AssertIdle
AssertSourcePins
if((GitResult '--no-optional-locks rev-parse HEAD').output.Trim() -ne $head -or (FileHash $indexPath) -ne $indexBefore) {throw 'HEAD/index changed before staging'}
$staged=$false
if($StageOwned) {
  $apply=GitResult ('--no-optional-locks apply --cached '+$manifest.patch)
  if($apply.exit_code -ne 0) {throw ('Owned staging failed: '+$apply.error)}
  $staged=$true
  $actual=@((GitResult '--no-optional-locks diff --cached --name-only -z').output.Split([char]0)|Where-Object {$_.Length -ne 0}|Sort-Object)
  $expected=@($manifest.owned_files|ForEach-Object {$_.path}|Sort-Object)
  if($actual.Count -ne 41 -or ($actual -join "`n") -cne ($expected -join "`n")) {throw 'Unexpected cached target set; retain index for root review'}
  foreach($row in $manifest.owned_files) {
    $blob=GitResult ('--no-optional-locks show :'+$row.path)
    if($blob.exit_code -ne 0) {throw ('Cannot inspect staged blob: '+$row.path)}
    $h=[Security.Cryptography.SHA256]::Create()
    try {$sha=([BitConverter]::ToString($h.ComputeHash($utf8.GetBytes($blob.output)))).Replace('-','').ToLowerInvariant()} finally {$h.Dispose()}
    if($sha -ne $row.stage_blob_sha256) {throw ('Unexpected cached content: '+$row.path)}
  }
}
AssertSourcePins
foreach($path in $excludedBefore.Keys) {if((FileHash (Join-Path $repo $path)) -ne $excludedBefore[$path]) {throw ('Unrelated worktree path changed: '+$path)}}
if((GitResult '--no-optional-locks rev-parse HEAD').output.Trim() -ne $head) {throw 'HEAD changed during staging'}
$receipt=[ordered]@{
  status=if($staged){'owned41_staged_index_only'}else{'owned41_checks_only_no_staging'}
  terminal=$true
  head=$head
  manifest=$manifestPath
  manifest_sha256=$manifestHash
  patch_sha256=$patchHash
  dependency_audit_sha256=$dependencyHash
  source_inventory_sha256=$inventoryHash
  validation=$ValidationPath
  validation_sha256=$ValidationSha256.ToLowerInvariant()
  validation_status=$validation.status
  validation_controller_sha256=$validationControllerHash
  historical_correctness_receipt_sha256=$priorReceiptHash
  historical_correctness_phase_count=16
  current_targeted_proof_sha256=$focusedReceiptHash
  full_validated=[bool]$validation.full_validated
  fixed_gate_sha256=$validation.fixed_gate.sha256
  official_pprint_sha256=$official.sha256
  official_pprint_exit_code=$official.exit_code
  official_pprint_complete=[bool]$official.complete
  official_pprint_partial_evidence_sha256=$official.partial_evidence_sha256
  performance_claim='Fixed gate passed; original official attempt recorded. A failed/timeout official attempt is unscored and does not establish a speedup or overall CPython win.'
  owned_target_count=41
  staged=$staged
  source_changes=$false
  preserved_dirty_before=$excludedBefore
  index_sha256_before=$indexBefore
  index_sha256_after=FileHash $indexPath
  committed=$false
  build_execution=$false
  runtime_execution=$false
}
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($ReceiptPath))|Out-Null
[IO.File]::WriteAllText($ReceiptPath,(($receipt|ConvertTo-Json -Depth 7)+"`n"),$utf8)
$receipt|ConvertTo-Json -Depth 3
