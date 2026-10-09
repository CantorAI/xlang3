$ErrorActionPreference='Stop'
$rootPath=(Get-Location).Path
$parentPath='scratch/performance/measure-pickle-published-frame-c6-original-body-paired-20261008.py'
$targetPath='scratch/performance/measure-dict-scalar-append-original-body-paired-proposed-20261008.py'
$proofPath='scratch/performance/dict-scalar-append-original-body-paired-manager-provenance-proposed-20261008.json'
if($rootPath -ne 'D:\CantorAI\xlang3'){throw 'Run from repository root'}
if((Test-Path -LiteralPath $targetPath) -or (Test-Path -LiteralPath $proofPath)){throw 'Preserve frozen output'}
$parentSha='b41f74b4efac72cf40e46ec9bccc6cabc2b31d203e9b764341e22a1109d26a0c'
if((Get-FileHash $parentPath -Algorithm SHA256).Hash.ToLower() -ne $parentSha){throw 'Parent pattern drift'}
$base=[IO.File]::ReadAllText((Join-Path $rootPath $parentPath)).Replace("`r`n","`n")
function Slice([string]$start,[string]$end){$a=$base.IndexOf($start);$b=$base.IndexOf($end,$a);if($a -lt 0 -or $b -lt 0){throw 'Missing reviewed pattern'};return $base.Substring($a,$b-$a)}
$helpers=Slice '    def idle(label):' "    try:`n        save(); idle('preflight')"
$deps=Slice '        for path,value in ((CHILD,CHILD_SHA)' '        # Successful initial C6 CP/X parity is retained, never rerun here.'
$tail=Slice '        pin(WATCH,WATCH_SHA)' '    except BaseException as error: record.update(status='
$tail=$tail.Replace("executables={'control':C5_CONTROL/'xlang3.exe','candidate':EXE}","executables={'parent':PARENT_RELEASE/'xlang3.exe','candidate':EXE}")
$tail=$tail.Replace("'control'","'parent'").Replace('ratio_c5_over_r6','ratio_parent_over_candidate').Replace('pair_ratios_c5_over_r6','pair_ratios_parent_over_candidate').Replace('reject_c6_retain_verified_c5','reject_candidate_retain_verified_parent')
$tail=$tail.Replace('control_seconds=','parent_seconds=')
$header=@'
"""Seven frozen-rule original pickle body pairs; root execution only.

Preserved validated R4 parent versus fixed candidate, unchanged 2460 dumps each.
CPython 3.14.7 manages only; no CP body, retry, trimming, score or automatic apply.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import math
import random
import statistics
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
DATA=ROOT/'doc/performance/data'
CP=Path('C:/Python/Python314/python.exe')
RELEASE=ROOT/'build-repro/main-verify-20261006/Release'
EXE=RELEASE/'xlang3.exe'
BASELINE=ROOT/'build-repro/Release'
PARENT_ROOT=ROOT/'build-repro/controls/dict-scalar-append-runtime-index-parent-20261008'
PARENT_RELEASE=PARENT_ROOT/'Release'
PARENT_MANIFEST_SHA='35738253af6bd1f41c2a8a6b83233f1007e1ec1d913e234e6d3f3ae971d07707'
DECISION=ROOT/'scratch/performance/dict-scalar-append-freshness-trial-decision-root-20261008.json'
DECISION_SHA='05aad9b5625bccdb31a4e2e06ee92b9c8dd7717eba465ff66d19968bba5be276'
PARENT_VALIDATION=DATA/'frame-locals-retirement-r4-full-validation-20261008.json'
PARENT_VALIDATION_SHA='b28809dcfc420906e3c3f656fdd9584140d0a80d778673a43342799ea64e75ec'
PARENT_INVENTORY=DATA/'frame-locals-retirement-r4-registered-source-20261008.json'
PARENT_INVENTORY_SHA='e696ed5b31449e7ed29c173df79de3b55292597418231f417af887574c60e4c8'
BODY=DATA/'pickle-frame-locals-retirement-r4-original-body-r2-20261008.json'
BODY_SHA='fab10dd8e053a9ca82448ba8eccc9e1cb339633602b6dcdb408d844aab882576'
CHILD=ROOT/'scratch/performance/pickle-original-pure-python-diagnostic-proposed-20261008.py'
CHILD_SHA='5295c899d95d023ca2fb3b511e9bf78b58cb49e4a256770c65adf0d614b14109'
BENCHMARK=CP.parent/'Lib/site-packages/pyperformance/data-files/benchmarks/bm_pickle/run_benchmark.py'
BENCHMARK_SHA='31c0e30be79514b45db0d4e858632eb429b0fea3edbac6463ad5f34d92f3cab8'
PICKLE_SHA='144fdf593814627e13a5fcb0947728e8f0775ecdec181046ee4609063cf232ec'
HOOK=ROOT/'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
HOOK_SHA='2ffc217fe20497156bfcb1cebc7fb87203f33c6d4d3dbb6ecbcbcf91ea4cb317'
SAMPLER=ROOT/'scratch/performance/sample-pprint-native-cpu-20261008.exe'
WATCH=ROOT/'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'
WATCH_SHA='50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf'
TARGETS={'src/runtime/mapping.cpp','tests/cpp/interpreter_tests.cpp','tests/cpp/dict_scalar_append_index_cases.h'}
PAIR_COUNT=7
ORDERS=[('parent','candidate') if index%2==0 else ('candidate','parent') for index in range(PAIR_COUNT)]
BOOTSTRAP_RESAMPLES=50000
BOOTSTRAP_SEED=20261008
MINIMUM_MEDIAN_GAIN=1.05
MINIMUM_LOWER_CI=1.0
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
read=lambda p:json.loads(Path(p).read_bytes())
tree=lambda root:{p.relative_to(root).as_posix():sha(p) for p in sorted(root.rglob('*')) if p.is_file()}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('source-inventory','focused-receipt','proposal-provenance','build-receipt'):
        parser.add_argument('--'+name,type=Path,required=True)
        parser.add_argument('--'+name+'-sha256',required=True)
    parser.add_argument('--prefix',required=True)
    args=parser.parse_args()
    assert sys.implementation.name=='cpython' and sys.version_info[:3]==(3,14,7) and not sys.flags.optimize
    assert Path(sys.executable).resolve()==CP.resolve() and ROOT==Path('D:/CantorAI/xlang3').resolve()
    assert re.fullmatch(r'[a-z0-9-]+',args.prefix) and not any(DATA.glob(args.prefix+'*'))
    output=DATA/(args.prefix+'.json'); pins={}; release=None; protected={}
    record=dict(status='preflight',terminal=False,mode='paired-original-body',diagnostic_only=True,
        scored=False,acceptance=False,full_validated=False,fixed_gate=None,
        scope='Seven original-body parent/candidate pairs; no official/CP score or automatic acceptance/restore',
        outer_loops=41,protocol=5,original_dump_operations=2460,profile_enabled=False,timeout_seconds=300,
        pair_count=7,pair_order=ORDERS,total_body_invocations=14,total_dump_operations=34440,
        raw=[],pair_results=[],idle_guards=[],values_trimmed=0,outliers_removed=0,
        decision_rule=dict(median_ratio_strictly_greater_than=1.05,lower_ci_strictly_greater_than=1.0,
            bootstrap_resamples=50000,bootstrap_seed=20261008,confidence_percent=95),started_utc=datetime.now(timezone.utc).isoformat())
    def save():output.write_bytes((json.dumps(record,indent=2)+'\n').encode('utf-8'))
    def pin(path,expected=None):
        path=Path(path).resolve(strict=True); value=sha(path)
        assert expected is None or value==expected,str(path)
        assert str(path) not in pins or pins[str(path)]==value
        pins[str(path)]=value; return value
    def release_map():return {p.relative_to(ROOT).as_posix():sha(p) for p in sorted(RELEASE.rglob('*')) if p.is_file()}
    def stable():return all(Path(p).is_file() and sha(p)==h for p,h in pins.items()) and (release is None or release_map()==release) and all(tree(Path(p))==v for p,v in protected.items())
'@
$preflight=@'
    try:
        save(); idle('preflight')
        for path,value in ((DECISION,DECISION_SHA),(PARENT_VALIDATION,PARENT_VALIDATION_SHA),(PARENT_INVENTORY,PARENT_INVENTORY_SHA),(BODY,BODY_SHA),
            (PARENT_ROOT/'preserved-release-provenance.json',PARENT_MANIFEST_SHA),
            (args.source_inventory,args.source_inventory_sha256),(args.focused_receipt,args.focused_receipt_sha256),
            (args.proposal_provenance,args.proposal_provenance_sha256),(args.build_receipt,args.build_receipt_sha256)):
            pin(path,value)
        decision,parent,base,body,manifest,inventory,focus,proposal,build=map(read,
            (DECISION,PARENT_VALIDATION,PARENT_INVENTORY,BODY,PARENT_ROOT/'preserved-release-provenance.json',
             args.source_inventory,args.focused_receipt,args.proposal_provenance,args.build_receipt))
        rule=decision['performance_screen']
        assert rule['order_balanced_parent_candidate_pairs']==7 and rule['retain_all_samples']
        assert (rule['required_median_speed_ratio_strictly_greater_than'],rule['required_bootstrap_95_percent_lower_bound_strictly_greater_than'])==(1.05,1.0)
        assert manifest['terminal'] and manifest['full_validated'] and manifest['status']=='preserved_validated_r4_scalar_append_trial_parent'
        assert manifest['decision_sha256']==DECISION_SHA and manifest['full_validation_sha256']==PARENT_VALIDATION_SHA
        assert (manifest['source_count'],manifest['file_count'])==(114,178)
        assert parent['terminal'] and parent['hashes_unchanged'] and parent['full_validated'] and parent['status']=='validated'
        assert parent['correctness_passed'] and parent['hashes_before']==parent['hashes_after'] and parent['fixed_gate']['exit_code']==0
        gate_path=DATA/parent['fixed_gate']['output']; pin(gate_path,parent['fixed_gate']['sha256']); gate=read(gate_path)
        assert gate['status']=='pass' and len(gate['cases'])==11 and (gate['repeats'],gate['warmup'],gate['threshold'])==(21,5,.1)
        assert parent['source_sha256']==base['source_sha256'] and all(manifest['source_snapshot_sha256'][p]==h for p,h in base['source_sha256'].items())
        assert proposal['raw_before_sha256']==manifest['source_snapshot_sha256'] and len(proposal['raw_before_sha256'])==114
        assert set(proposal['candidate_source_sha256'])==TARGETS and proposal['new_owned_targets']==['tests/cpp/dict_scalar_append_index_cases.h']
        expected_sources=dict(manifest['source_snapshot_sha256'],**proposal['candidate_source_sha256'])
        assert inventory['source_sha256']==expected_sources and len(expected_sources)==115
        pin(ROOT/proposal['patch'],proposal['patch_sha256'])
        for path,h in proposal['candidate_source_sha256'].items():pin(ROOT/proposal['candidate_root']/path,h)
        for path,h in expected_sources.items():pin(ROOT/path,h)
        cpp_text=(ROOT/'tests/cpp/interpreter_tests.cpp').read_text(encoding='utf-8')
        assert cpp_text.count('#include "dict_scalar_append_index_cases.h"')==1
        assert cpp_text.count('xlang3::test::check_dict_scalar_append_index_cases(result);')==1
        assert focus['terminal'] and focus['status']=='targeted_correctness_passed' and focus['hashes_unchanged']
        assert focus['source_inventory_sha256']==args.source_inventory_sha256 and focus['source_sha256']==expected_sources
        assert focus['phases'] and len({row['name'] for row in focus['phases']})==len(focus['phases'])
        cpp=[row for row in focus['phases'] if row['name']=='cpp']; assert len(cpp)==1
        assert cpp[0]['command']==[str(RELEASE/'xlang3_interpreter_tests.exe')]
        for row in focus['phases']:
            assert row['passed'] and row['exit_code']==0 and not row.get('timeout',False)
            for stream in ('stdout','stderr'):pin(DATA/row[stream+'_log'],row[stream+'_sha256'])
            assert (DATA/row['stderr_log']).read_bytes()==b''
            if row['name']!='cpp':
                assert row['output_matches_expected']
                for field in ('source','expected'):pin(ROOT/row[field],row[field+'_sha256'])
                assert row['command']==[str(EXE),str(ROOT/row['source'])]
                assert (DATA/row['stdout_log']).read_bytes().replace(b'\r\n',b'\n')==(ROOT/row['expected']).read_bytes().replace(b'\r\n',b'\n')
        assert build['terminal'] and build['status']=='build_completed' and build['exit_code']==0
        assert build.get('source_inventory_sha256',build.get('application_receipt_sha256'))==args.source_inventory_sha256
        assert isinstance(build['command'],list) and build['command']
        pin(ROOT/build['build_log'],build['build_log_sha256'])
        if 'wrapper' in build:pin(ROOT/build['wrapper'],build['wrapper_sha256'])
        release=release_map(); assert len(release)==178 and release==focus['binaries_sha256']
        assert sha(EXE)==focus['candidate_binary_sha256']['exe'] and sha(EXE.with_name('xlang3_runtime.dll'))==focus['candidate_binary_sha256']['dll']
        for name in ('xlang3_runtime.dll','xlang3_interpreter_tests.exe'):assert sha(RELEASE/name)!=manifest['files_sha256'][name]
        for path,h in release.items():pin(ROOT/path,h)
        for directory,values in ((PARENT_RELEASE,manifest['files_sha256']),(PARENT_ROOT/'source-snapshot',manifest['source_snapshot_sha256']),(BASELINE,manifest['fixed_baseline_sha256'])):
            assert tree(directory)==values
            protected[str(directory)]=values
            for path,h in values.items():pin(directory/path,h)
        assert len(manifest['fixed_baseline_sha256'])==177 and manifest['fixed_baseline_sha256']==parent['baseline_sha256']
        protected[str(PARENT_ROOT)]=dict({'Release/'+p:h for p,h in manifest['files_sha256'].items()},
            **{'source-snapshot/'+p:h for p,h in manifest['source_snapshot_sha256'].items()},**{'preserved-release-provenance.json':PARENT_MANIFEST_SHA})
        assert tree(PARENT_ROOT)==protected[str(PARENT_ROOT)]
        assert body['terminal'] and body['hashes_unchanged'] and body['status']=='terminal_unscored_original_body_match'
        assert body['hashes_before']==body['hashes_after'] and body['source_sha256']==parent['source_sha256']
        assert body['child_sha256']==CHILD_SHA and len(body['raw'])==2 and all(row['passed'] and row['exit_code']==0 and not row['timeout'] for row in body['raw'])
        assert body['binaries_sha256']=={RELEASE.relative_to(ROOT).as_posix()+'/'+p:h for p,h in manifest['files_sha256'].items()}
        for row in body['raw']:
            for stream in ('stdout','stderr'):pin(DATA/row[stream+'_log'],row[stream+'_sha256'])
        record.update(result_signature=body['result_signature'],parent_manifest_sha256=PARENT_MANIFEST_SHA,
            frozen_decision_sha256=DECISION_SHA,proposal_provenance_sha256=args.proposal_provenance_sha256,
            source_inventory_sha256=args.source_inventory_sha256,source_sha256=expected_sources,binaries_sha256=release,
            focused_receipt_sha256=args.focused_receipt_sha256,build_receipt_sha256=args.build_receipt_sha256,
            parent_source_sha256=manifest['source_snapshot_sha256'],parent_release_sha256=manifest['files_sha256'],
            frozen_original_parity_receipt_sha256=BODY_SHA,cpython_body_rerun=False)
'@
$finish=@'
    except BaseException as error:record.update(status='terminal_failed_diagnostic_controller',error=type(error).__name__+': '+str(error))
    finally:
        record['hashes_after']={p:sha(p) if Path(p).is_file() else None for p in pins}
        record['release_after']=release_map()
        record['hashes_unchanged']=stable(); record['input_verification_complete']=release is not None
        if not record['hashes_unchanged']:record['status']='terminal_invalid_hash_drift'
        record.update(terminal=True,completed_utc=datetime.now(timezone.utc).isoformat());save()
    return 0 if record['status']=='terminal_unscored_paired_original_body_diagnostic' else 1

if __name__=='__main__':raise SystemExit(main())
'@
$text=$header+"`n"+$helpers+$preflight+"`n"+$deps+$tail+$finish+"`n"
[IO.File]::WriteAllText((Join-Path $rootPath $targetPath),$text,[Text.UTF8Encoding]::new($false))
$proof=[ordered]@{status='frozen_scratch_only_unexecuted_conditional_original_body_pairs';controller=$targetPath;controller_sha256=(Get-FileHash $targetPath -Algorithm SHA256).Hash.ToLower();reviewed_pattern=$parentPath;reviewed_pattern_sha256=$parentSha;decision_sha256='05aad9b5625bccdb31a4e2e06ee92b9c8dd7717eba465ff66d19968bba5be276';parent_manifest_sha256='35738253af6bd1f41c2a8a6b83233f1007e1ec1d913e234e6d3f3ae971d07707';final_proposal_inventory_focus_build='Required explicit caller path+SHA; exact three target maps/raw114 parent/candidate115; fresh registered CPP phase plus every supplied successful strict Python row; no guessed future SHA';child_sha256='5295c899d95d023ca2fb3b511e9bf78b58cb49e4a256770c65adf0d614b14109';parent_signature_receipt_sha256='fab10dd8e053a9ca82448ba8eccc9e1cb339633602b6dcdb408d844aab882576';pair_count=7;child_count=14;original_workload='41*3*20=2460 dumps per child, protocol5, no profiling/input/work changes';threshold_median_strictly_greater_than=1.05;threshold_bootstrap95_lower_strictly_greater_than=1.0;bootstrap_resamples=50000;bootstrap_seed=20261008;strict_watcher_sha256='50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf';retained_raw='All fourteen xb split logs/partials/watcher records, no repeats/trimming; stop first invalid child';full_validation='Held for useful paired signal; root adapts existing R4 fresh full/default11gate21/5/.10/originalpurepickle20 controller afterward';runtime_python_ast_build_or_timing_execution_by_author=$false;actual_engine_or_test_edits_by_author=$false}
[IO.File]::WriteAllText((Join-Path $rootPath $proofPath),($proof|ConvertTo-Json -Depth 8)+"`n",[Text.UTF8Encoding]::new($false))
[pscustomobject]@{controller=$targetPath;sha256=$proof.controller_sha256;proof=$proofPath;proof_sha256=(Get-FileHash $proofPath -Algorithm SHA256).Hash.ToLower()}|ConvertTo-Json -Compress
