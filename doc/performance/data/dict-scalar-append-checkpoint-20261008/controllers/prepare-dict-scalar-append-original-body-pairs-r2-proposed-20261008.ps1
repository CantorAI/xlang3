$ErrorActionPreference='Stop'
$rootPath=(Get-Location).Path
if($rootPath -ne 'D:\CantorAI\xlang3'){throw 'Run from repository root'}
$parentPath='scratch/performance/measure-dict-scalar-append-original-body-paired-proposed-20261008.py'
$parentSha='5ce1840d345c1b502beda7c97c43a122ed62662125c93ac21ab204623aa82a55'
$oldProofPath='scratch/performance/dict-scalar-append-original-body-paired-manager-provenance-proposed-20261008.json'
$oldProofSha='083978f018d6a96dfc8d6be1f26dc2d0f0849b8ff8c189ac4df67ba6976afbac'
$targetPath='scratch/performance/measure-dict-scalar-append-original-body-paired-r2-proposed-20261008.py'
$proofPath='scratch/performance/dict-scalar-append-original-body-paired-manager-r2-provenance-proposed-20261008.json'
foreach($p in @($targetPath,$proofPath)){if(Test-Path -LiteralPath $p){throw 'Preserve frozen output'}}
if((Get-FileHash $parentPath -Algorithm SHA256).Hash.ToLower() -ne $parentSha){throw 'Frozen parent drift'}
if((Get-FileHash $oldProofPath -Algorithm SHA256).Hash.ToLower() -ne $oldProofSha){throw 'Frozen proof drift'}
$old=@'
                finally:
                    if child is not None and child.poll() is None:
                        subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)],capture_output=True,timeout=15)
                        if child.poll() is None: child.kill()
                        child.wait(timeout=15)
                    for stream,path in (('stdout',stdout),('stderr',stderr)):
                        row[stream+'_sha256']=sha(path) if path.is_file() else None
                    row['measurement_valid']=finish_watch()
                    row['passed']=row['passed'] and row['measurement_valid']
                    save()
'@
$new=@'
                finally:
                    try:
                        if child is not None and child.poll() is None:
                            subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)],capture_output=True,timeout=15)
                            if child.poll() is None: child.kill()
                            child.wait(timeout=15)
                        row['owned_child_cleanup_completed']=True
                    except BaseException as error:
                        row['owned_child_cleanup_completed']=False
                        row['cleanup_error']=type(error).__name__+': '+str(error)
                        row['passed']=False
                    finally:
                        for stream,path in (('stdout',stdout),('stderr',stderr)):
                            try:row[stream+'_sha256']=sha(path) if path.is_file() else None
                            except BaseException as error:
                                row[stream+'_sha256']=None
                                row[stream+'_hash_error']=type(error).__name__+': '+str(error)
                                row['passed']=False
                        try:row['measurement_valid']=finish_watch()
                        except BaseException as error:
                            row['measurement_valid']=False
                            row['watch_finish_error']=type(error).__name__+': '+str(error)
                        row['passed']=row['passed'] and row['measurement_valid']
                        save()
'@
$text=[IO.File]::ReadAllText((Join-Path $rootPath $parentPath)).Replace("`r`n","`n")
$old=$old.Replace("`r`n","`n");$new=$new.Replace("`r`n","`n")
if($text.IndexOf($old) -lt 0 -or $text.IndexOf($old) -ne $text.LastIndexOf($old)){throw 'Bounded cleanup pattern changed'}
$text=$text.Replace($old,$new)
[IO.File]::WriteAllText((Join-Path $rootPath $targetPath),$text,[Text.UTF8Encoding]::new($false))
$prior=Get-Content -Raw -LiteralPath $oldProofPath|ConvertFrom-Json
$proof=[ordered]@{}
foreach($p in $prior.PSObject.Properties){$proof[$p.Name]=$p.Value}
$proof.controller=$targetPath
$proof.controller_sha256=(Get-FileHash $targetPath -Algorithm SHA256).Hash.ToLower()
$proof.supersedes_preserved_controller=$parentPath
$proof.supersedes_preserved_controller_sha256=$parentSha
$proof.supersedes_preserved_proof_sha256=$oldProofSha
$proof.bounded_delta='Only owned-child cleanup finally: cleanup errors invalidate row; split-stream hashing and timing watcher finish always attempted, including cleanup failure. No workload, process policy, count, decision threshold, input guard or pairing change.'
$proof.final_proposal_contract='Caller final composed provenance raw_before_sha256 must equal all114 preserved parent hashes, candidate_source_sha256 all3 final composed targets; an incremental raw115 proof alone is insufficient.'
[IO.File]::WriteAllText((Join-Path $rootPath $proofPath),($proof|ConvertTo-Json -Depth 12)+"`n",[Text.UTF8Encoding]::new($false))
[pscustomobject]@{controller=$targetPath;sha256=$proof.controller_sha256;proof=$proofPath;proof_sha256=(Get-FileHash $proofPath -Algorithm SHA256).Hash.ToLower()}|ConvertTo-Json -Compress
