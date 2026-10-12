param(
  [Parameter(Mandatory=$true)][string]$PlanSha256,
  [switch]$Publish
)
$ErrorActionPreference='Stop'
$exportRoot='D:/CantorAI/xlang3'
$exportPlanPath=Join-Path $exportRoot 'scratch/performance/r6-r7-s8-publication-plan-final-proposed-20261008.json'
$exportUtf8=[System.Text.UTF8Encoding]::new($false)
function FileSha([string]$path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
if((FileSha $exportPlanPath)-ne$PlanSha256){throw 'Final publication plan SHA mismatch'}
$exportPlan=Get-Content -LiteralPath $exportPlanPath -Raw|ConvertFrom-Json
if($exportPlan.status-ne'final_frozen_root_review_only'){throw 'Only frozen terminal evidence may export'}
$exportFull=Get-Content -LiteralPath (Join-Path $exportRoot $exportPlan.full_validation.path) -Raw|ConvertFrom-Json
if((FileSha (Join-Path $exportRoot $exportPlan.full_validation.path))-ne$exportPlan.full_validation.sha256){throw 'Full validation changed'}
if(-not($exportFull.terminal-and$exportFull.correctness_passed-and$exportFull.hashes_unchanged-and$exportFull.release_tree_unchanged-and$exportFull.status-eq'validated'-and$exportFull.fixed_gate.exit_code-eq0)){throw 'Current full correctness/default gate proof required'}
$exportSource=Get-Content -LiteralPath (Join-Path $exportRoot $exportPlan.source_inventory) -Raw|ConvertFrom-Json
if((FileSha (Join-Path $exportRoot $exportPlan.source_inventory))-ne$exportPlan.source_inventory_sha256-or$exportSource.source_count-ne110){throw 'Source inventory changed'}
foreach($exportProp in $exportSource.source_sha256.PSObject.Properties){if((FileSha (Join-Path $exportRoot $exportProp.Name))-ne$exportProp.Value){throw ('Actual source drift '+$exportProp.Name)}}
foreach($exportProp in $exportFull.binaries_sha256.PSObject.Properties){if((FileSha (Join-Path $exportRoot $exportProp.Name))-ne$exportProp.Value){throw ('Release drift '+$exportProp.Name)}}
$exportActualRelease=@(Get-ChildItem -LiteralPath (Join-Path $exportRoot 'build-repro/main-verify-20261006/Release') -File -Recurse)
if($exportActualRelease.Count-ne178-or@($exportFull.binaries_sha256.PSObject.Properties).Count-ne178){throw 'Complete Release inventory changed'}
$exportRows=@($exportPlan.doc_files)+@([pscustomobject]@{path=$exportPlanPath;sha256=$PlanSha256;destination='doc/performance/data/r6-r7-s8-checkpoint-inspection-20261008/final-publication-plan.json'})
foreach($exportRow in @($exportPlan.owned_sources)+$exportRows+@($exportPlan.attributes_inputs)){
  $exportInput=if([System.IO.Path]::IsPathRooted($exportRow.path)){$exportRow.path}else{Join-Path $exportRoot $exportRow.path}
  if((FileSha $exportInput)-ne$exportRow.sha256){throw ('Frozen file changed '+$exportRow.path)}
}
$exportOutput=if($Publish){$exportRoot}else{Join-Path $exportRoot 'scratch/performance/r6-r7-s8-checkpoint-export-preview-20261008/publication'}
if(-not$Publish-and(Test-Path -LiteralPath $exportOutput)){throw 'Preview exists; preserve it instead of repeating'}
$exportExpected=[ordered]@{}
foreach($exportRow in $exportRows){
  if(-not$exportRow.destination.StartsWith('doc/performance/')-or$exportRow.destination.Contains('..')){throw 'Destination outside owned documentation'}
  if($exportExpected.Contains($exportRow.destination)){throw 'Duplicate destination'}
  $exportExpected[$exportRow.destination]=$exportRow.sha256
  $exportTarget=[System.IO.Path]::GetFullPath((Join-Path $exportOutput $exportRow.destination))
  if(-not$exportTarget.StartsWith([System.IO.Path]::GetFullPath($exportOutput)+[System.IO.Path]::DirectorySeparatorChar,[System.StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe export destination'}
  if(Test-Path -LiteralPath $exportTarget){if((FileSha $exportTarget)-ne$exportRow.sha256){throw ('Refuse overwrite '+$exportRow.destination)}}
}
foreach($exportRow in $exportRows){
  $exportInput=if([System.IO.Path]::IsPathRooted($exportRow.path)){$exportRow.path}else{Join-Path $exportRoot $exportRow.path}
  $exportTarget=Join-Path $exportOutput $exportRow.destination
  if(-not(Test-Path -LiteralPath $exportTarget)){
    [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($exportTarget))|Out-Null
    [System.IO.File]::WriteAllBytes($exportTarget,[System.IO.File]::ReadAllBytes($exportInput))
  }
  if((FileSha $exportTarget)-ne$exportRow.sha256){throw 'Copied bytes differ'}
}
foreach($exportProp in $exportSource.source_sha256.PSObject.Properties){if((FileSha (Join-Path $exportRoot $exportProp.Name))-ne$exportProp.Value){throw 'Source drift during report export'}}
foreach($exportProp in $exportFull.binaries_sha256.PSObject.Properties){if((FileSha (Join-Path $exportRoot $exportProp.Name))-ne$exportProp.Value){throw 'Binary drift during report export'}}
$exportManifestRelative='doc/performance/data/r6-r7-s8-checkpoint-20261008-publication-manifest.json'
$exportManifest=[ordered]@{status='byte_exact_doc_export_root_review_required';plan_sha256=$PlanSha256;source_inventory_sha256=$exportPlan.source_inventory_sha256;files=$exportExpected;file_count=$exportExpected.Count;owned_source_paths=@($exportPlan.owned_sources.path);owned_source_count=8;gitattributes='Stage only the separately frozen HEAD-plus-owned blob; preserve unowned working attributes';no_git_stage_or_commit_executed=$true}
$exportManifestBytes=$exportUtf8.GetBytes(($exportManifest|ConvertTo-Json -Depth 8)+"`n")
$exportManifestTarget=Join-Path $exportOutput $exportManifestRelative
if(Test-Path -LiteralPath $exportManifestTarget){
  $exportExisting=[System.IO.File]::ReadAllBytes($exportManifestTarget)
  if([Convert]::ToBase64String($exportExisting)-ne[Convert]::ToBase64String($exportManifestBytes)){throw 'Preserve existing manifest'}
}else{[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($exportManifestTarget))|Out-Null;[System.IO.File]::WriteAllBytes($exportManifestTarget,$exportManifestBytes)}
$exportStagePaths=@($exportExpected.Keys)+@($exportManifestRelative)+@($exportPlan.owned_sources.path)
$exportStagePaths=@($exportStagePaths|Sort-Object -Unique)
if($exportStagePaths-contains'.gitattributes'){throw 'Never stage the unowned working attribute diff through this list'}
$exportListRoot=Join-Path $exportRoot 'scratch/performance/r6-r7-s8-checkpoint-export-preview-20261008'
[System.IO.Directory]::CreateDirectory($exportListRoot)|Out-Null
$exportListTarget=Join-Path $exportListRoot 'staging-paths.nul'
$exportListBytes=$exportUtf8.GetBytes(($exportStagePaths-join[char]0)+[char]0)
if(Test-Path -LiteralPath $exportListTarget){if([Convert]::ToBase64String([System.IO.File]::ReadAllBytes($exportListTarget))-ne[Convert]::ToBase64String($exportListBytes)){throw 'Preserve old staging list'}}else{[System.IO.File]::WriteAllBytes($exportListTarget,$exportListBytes)}
[ordered]@{status='exported_for_root_review_no_staging';published=[bool]$Publish;doc_files=$exportExpected.Count;owned_source_files=8;staging_paths=$exportStagePaths.Count;manifest_sha256=FileSha $exportManifestTarget;staging_nul_sha256=FileSha $exportListTarget;output=$exportOutput}|ConvertTo-Json -Compress
