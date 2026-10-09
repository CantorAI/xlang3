"""Seven frozen-rule original pickle body pairs; root execution only.

Preserved validated native UTF-8 parent versus fixed candidate, unchanged 2460 dumps each.
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
PARENT_ROOT=ROOT/'build-repro/controls/native-str-utf8-encode-parent-20261008'
PARENT_RELEASE=PARENT_ROOT/'Release'
PARENT_MANIFEST_SHA='cd5035f531a513104f2948286e81d4996bb01a9b6dc4c84353303f370de4bd97'
PARENT_VALIDATION=DATA/'dict-scalar-append-runtime-index-r3-validation-20261008.json'
PARENT_VALIDATION_SHA='0b6f05e17c2878efa135656abcce38338934069b59a9460de9f2ba98a8516dd0'
PARENT_INVENTORY=DATA/'dict-scalar-append-runtime-index-r3-applied-source-20261008.json'
PARENT_INVENTORY_SHA='dc1b869fe0475e3bdd017aa3b667f755cb7a4dd7ad881494f8afb34af387f45e'
BODY=DATA/'dict-scalar-append-original-body-paired-20261008.json'
BODY_SHA='c97989d6589be1eb721e608978cc69ae01227b190630ffe4f4e84564fcd1fc0e'
BODY_CONTROLLER=ROOT/'scratch/performance/measure-dict-scalar-append-original-body-paired-r2-proposed-20261008.py'
BODY_CONTROLLER_SHA='de0aaf91f5c31f3e14d6a18d780440670c44dfbf157bd3c608d545f591852f0f'
ADDITIONAL_PARENT_SOURCES={'src/runtime/methods/string_methods.cpp':'52860b33aaf0dcdc7fbe4f991725f31e21ba722bfce7dcceccbffa5fcc0ec4ae',
    'src/runtime/modules/system/codecs_module.cpp':'6a0184a50ac0026fc0c54a3bff5da3f26961aaa1e76e2b7cc2a8d6cd1848c7f1'}
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
TARGETS={'src/runtime/methods/string_methods.cpp','src/runtime/modules/system/codecs_module.cpp','src/internal/xlang3/builtins.h',
    'tests/run_fixtures.py','tests/run_fixtures.ps1','tests/fixtures/core/native_str_utf8_encode.py',
    'tests/fixtures/expected/native_str_utf8_encode.out'}
DECISION=ROOT/'scratch/performance/native-str-utf8-encode-trial-decision-proposed-20261008.json'
DECISION_SHA='7c0516afb8dea0ecbede40bde8bc8361dd4da2d9728e1ecdb1970c5af8db3fcb'
ENCODING_REFERENCE=DATA/'native-str-utf8-cpython-semantics-20261008.json'
ENCODING_REFERENCE_SHA='f3c91eb53ca3f903838367c2bd111b72ba778ae36d507d88b4f4fa95d0f313e4'
PAIR_COUNT=7
ORDERS=[('parent','candidate') if index%2==0 else ('candidate','parent') for index in range(PAIR_COUNT)]
BOOTSTRAP_RESAMPLES=50000
BOOTSTRAP_SEED=20261008
MINIMUM_MEDIAN_GAIN=1.05
MINIMUM_LOWER_CI=1.0
FOCUS_PHASES=['cpp','native_str_utf8_encode','string_compat','unicode_repr_surrogate','pickle_module',
    'sys_monitoring_all_events','trace_hooks','nested_profile_setting']
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
    def idle(label):
        raw = subprocess.check_output(['powershell', '-NoProfile', '-Command',
            'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'])
        rows = json.loads(raw.decode('utf-8-sig') or '[]')
        if isinstance(rows, dict): rows = [rows]
        tools = {'cl.exe','link.exe','ninja.exe','msbuild.exe','cmake.exe','ctest.exe','nmake.exe','lld-link.exe','clang-cl.exe',SAMPLER.name.lower()}
        busy = [r for r in rows if r['ProcessId'] != os.getpid() and
            (r['Name'].lower() in tools or r['Name'].lower().startswith(('python','xlang3')))]
        record['idle_guards'].append(dict(phase=label, allowed_controller_pid=os.getpid(), busy=busy)); save()
        assert not busy, busy
    def validate_child(stdout, stderr, role, executable, row):
        events = [json.loads(line) for line in stdout.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
        row['events'] = events
        assert len(events) == 2 and events[0]['status'] == 'body_start'
        event = events[-1]
        assert event['status'] == 'body_complete' and event['success'] and event['hashes_unchanged']
        assert stderr.read_bytes() == b'' and event['runtime'] == role
        assert event['version_info'] == [3,14,7] and Path(event['executable']).resolve() == executable.resolve()
        assert event['optimization_level'] == 0 and not event['profile_enabled'] and all(event['prechecks'].values())
        assert event['benchmark_source_sha256'] == BENCHMARK_SHA and event['pickle_source_sha256'] == PICKLE_SHA
        assert event['outer_loops'] == 41 and event['protocol'] == 5 and event['repeat'] == 1
        assert event['object_count'] == 3 and event['inner_loops_metadata'] == 20 and event['total_dumps_in_original_body'] == 2460
        assert event['input_and_implementation_identity_preserved'] and event['signature_before'] == event['signature_after']
        assert [r['name'] for r in event['signature_after']] == ['DICT','TUPLE','DICT_GROUP']
        assert all(r['roundtrip_equal'] and r['byte_length'] > 0 for r in event['signature_after'])
        row.update(result_signature=event['signature_after'], elapsed_seconds_diagnostic_only=event['elapsed_seconds_diagnostic_only'],
            original_timer_seconds_diagnostic_only=event['original_timer_seconds_diagnostic_only'])
        return event
    try:
        save(); idle('preflight')
        for path,value in ((DECISION,DECISION_SHA),(ENCODING_REFERENCE,ENCODING_REFERENCE_SHA),(PARENT_VALIDATION,PARENT_VALIDATION_SHA),(PARENT_INVENTORY,PARENT_INVENTORY_SHA),(BODY,BODY_SHA),
            (BODY_CONTROLLER,BODY_CONTROLLER_SHA),(PARENT_ROOT/'preserved-release-provenance.json',PARENT_MANIFEST_SHA),
            (args.source_inventory,args.source_inventory_sha256),(args.focused_receipt,args.focused_receipt_sha256),
            (args.proposal_provenance,args.proposal_provenance_sha256),(args.build_receipt,args.build_receipt_sha256)):
            pin(path,value)
        parent,base,body,manifest,inventory,focus,proposal,build=map(read,
            (PARENT_VALIDATION,PARENT_INVENTORY,BODY,PARENT_ROOT/'preserved-release-provenance.json',
             args.source_inventory,args.focused_receipt,args.proposal_provenance,args.build_receipt))
        assert manifest['terminal'] and manifest['full_validated'] and manifest['status']=='preserved_validated_native_str_utf8_parent'
        assert manifest['validation_sha256']==PARENT_VALIDATION_SHA
        assert (manifest['source_count'],manifest['file_count'],manifest['object_count'])==(117,178,149)
        assert parent['terminal'] and parent['hashes_unchanged'] and parent['full_validated'] and parent['status']=='trial_validated'
        assert parent['source_inventory_sha256']==PARENT_INVENTORY_SHA and parent['hashes_before']==parent['hashes_after']
        gate_path=DATA/parent['fixed_gate']['output']; pin(gate_path,parent['fixed_gate']['sha256']); gate=read(gate_path)
        assert parent['fixed_gate']['exit_code']==0 and gate['status']=='pass' and len(gate['cases'])==11
        assert (gate['repeats'],gate['warmup'],gate['threshold'])==(21,5,.1)
        assert parent['source_sha256']==base['source_sha256'] and len(base['source_sha256'])==115
        assert manifest['source_snapshot_sha256']==dict(base['source_sha256'],**ADDITIONAL_PARENT_SOURCES)
        assert proposal['raw_before_sha256']==manifest['source_snapshot_sha256']
        candidates=proposal['candidate_source_sha256']; new_targets=proposal['new_owned_targets']
        assert set(candidates)==TARGETS and set(new_targets)=={
            'tests/fixtures/core/native_str_utf8_encode.py','tests/fixtures/expected/native_str_utf8_encode.out'}
        assert proposal['resulting_recorded_source_count']==119 and proposal['resulting_core_fixture_count']==399
        decision=read(DECISION); rule=decision['screen']
        assert decision['parent_manifest_sha256']==PARENT_MANIFEST_SHA
        assert (rule['pairs'],rule['children'],rule['median_strictly_greater_than'],rule['lower95_strictly_greater_than'],
            rule['bootstrap_resamples'],rule['bootstrap_seed'])==(7,14,1.05,1.0,50000,20261008)
        assert rule['retain_all_samples'] and rule['alternating_order'] and not rule['adaptive_counts']
        assert not rule['retries'] and not rule['outlier_cuts']
        pin(ROOT/decision['diagnostic'],decision['diagnostic_sha256'])
        reference=read(ENCODING_REFERENCE); semantic=proposal['semantic_fixture']
        assert reference['terminal'] and reference['status']=='cpython_semantics_passed' and reference['passed']
        assert reference['exit_code']==0 and reference['hashes_unchanged'] and semantic['groups']==8
        assert reference['fixture_sha256']==semantic['source_sha256']==candidates['tests/fixtures/core/native_str_utf8_encode.py']
        assert reference['expected_sha256']==semantic['expected_sha256']==candidates['tests/fixtures/expected/native_str_utf8_encode.out']
        pin(ROOT/semantic['source'],semantic['source_sha256']); pin(ROOT/semantic['expected'],semantic['expected_sha256'])
        for stream in ('stdout','stderr'):pin(DATA/reference[stream+'_log'],reference[stream+'_sha256'])
        assert (DATA/reference['stderr_log']).read_bytes()==b''
        assert (DATA/reference['stdout_log']).read_bytes().replace(b'\r\n',b'\n')==(ROOT/semantic['expected']).read_bytes().replace(b'\r\n',b'\n')
        new_cases=['native_str_utf8_encode']
        expected_sources=dict(manifest['source_snapshot_sha256'],**candidates)
        assert inventory['source_sha256']==expected_sources and len(expected_sources)==119
        pin(ROOT/proposal['patch'],proposal['patch_sha256'])
        for path,h in candidates.items():pin(ROOT/proposal['candidate_root']/path,h)
        for path,h in expected_sources.items():pin(ROOT/path,h)
        suite_text=(ROOT/'tests/run_fixtures.py').read_text(encoding='utf-8')
        ps_text=(ROOT/'tests/run_fixtures.ps1').read_text(encoding='utf-8')
        for name in new_cases:
            assert len(re.findall(r'^'+re.escape(name)+r'\s*$',suite_text,re.MULTILINE))==1
            assert len(re.findall(r'^\s*"'+re.escape(name)+r'",\s*$',ps_text,re.MULTILINE))==1
        assert focus['terminal'] and focus['status']=='targeted_correctness_passed' and focus['hashes_unchanged']
        assert focus['source_inventory_sha256']==args.source_inventory_sha256 and focus['source_sha256']==expected_sources
        assert [row['name'] for row in focus['phases']]==FOCUS_PHASES
        assert focus['cpython_preflight_sha256']==ENCODING_REFERENCE_SHA
        assert focus['fixed_baseline_sha256']==manifest['fixed_baseline_sha256']
        cpp=[row for row in focus['phases'] if row['name']=='cpp']; assert len(cpp)==1
        assert cpp[0]['command']==[str(RELEASE/'xlang3_interpreter_tests.exe')]
        for row in focus['phases']:
            assert row['passed'] and row['exit_code']==0 and not row.get('timeout',False)
            for stream in ('stdout','stderr'):pin(DATA/row[stream+'_log'],row[stream+'_sha256'])
            assert (DATA/row['stderr_log']).read_bytes()==b''
            if row['name']!='cpp':
                if 'output_matches_expected' in row:assert row['output_matches_expected']
                for field in ('source','expected'):pin(ROOT/row[field],row[field+'_sha256'])
                assert row['command']==[str(EXE),str(ROOT/row['source'])]
                assert (DATA/row['stdout_log']).read_bytes().replace(b'\r\n',b'\n')==(ROOT/row['expected']).read_bytes().replace(b'\r\n',b'\n')
        assert build['terminal'] and build['status']=='build_passed' and build['exit_code']==build['actual_root_tool_exit_code']==0
        assert build['applied_source_sha256']==args.source_inventory_sha256
        assert build['build_directory']=='build-repro/main-verify-20261006' and build['configuration']=='Release'
        assert isinstance(build['session_id'],int) and build['session_id']>0
        pin(ROOT/build['log'],build['log_sha256'])
        release=release_map(); assert len(release)==178 and release==focus['binaries_sha256']
        assert sha(EXE)==focus['candidate_binary_sha256']['exe'] and sha(EXE.with_name('xlang3_runtime.dll'))==focus['candidate_binary_sha256']['dll']
        for name in new_cases:
            matching=[row for row in focus['phases'] if row.get('source')=='tests/fixtures/core/'+name+'.py']
            assert len(matching)==1 and matching[0]['expected']=='tests/fixtures/expected/'+name+'.out'
        assert sha(RELEASE/'xlang3_runtime.dll')!=manifest['files_sha256']['xlang3_runtime.dll']

        for path,h in release.items():pin(ROOT/path,h)
        for directory,values in ((PARENT_RELEASE,manifest['files_sha256']),(PARENT_ROOT/'source-snapshot',manifest['source_snapshot_sha256']),(BASELINE,manifest['fixed_baseline_sha256'])):
            assert tree(directory)==values
            protected[str(directory)]=values
            for path,h in values.items():pin(directory/path,h)
        assert len(manifest['fixed_baseline_sha256'])==177
        protected[str(PARENT_ROOT)]=dict({'Release/'+p:h for p,h in manifest['files_sha256'].items()},
            **{'source-snapshot/'+p:h for p,h in manifest['source_snapshot_sha256'].items()},
            **{'native-objects/'+p:h for p,h in manifest['object_snapshot_sha256'].items()},
            **{'build-metadata/'+p:h for p,h in manifest['build_metadata_sha256'].items()},
            **{'preserved-release-provenance.json':PARENT_MANIFEST_SHA})
        assert tree(PARENT_ROOT)==protected[str(PARENT_ROOT)]
        assert body['terminal'] and body['hashes_unchanged'] and body['status']=='terminal_unscored_paired_original_body_diagnostic'
        assert body['hashes_before']==body['hashes_after'] and body['source_sha256']==parent['source_sha256']
        assert body['controller_sha256']==BODY_CONTROLLER_SHA and body['child_sha256']==CHILD_SHA
        assert len(body['raw'])==14 and len(body['pair_results'])==7 and body['values_trimmed']==body['outliers_removed']==0
        assert body['binaries_sha256']=={RELEASE.relative_to(ROOT).as_posix()+'/'+p:h for p,h in manifest['files_sha256'].items()}
        for row in body['raw']:
            assert row['passed'] and row['exit_code']==0 and not row['timeout'] and row['measurement_valid']
            assert row['events'][-1]['signature_after']==body['result_signature']
            assert row['events'][-1]['total_dumps_in_original_body']==2460
            for stream in ('stdout','stderr'):pin(DATA/row[stream+'_log'],row[stream+'_sha256'])
            watch=row['external_process_watch']; assert watch['measurement_valid'] and not watch['overlaps'] and not watch['scanner_errors']
            pin(DATA/watch['log'],watch['sha256'])
        record.update(result_signature=body['result_signature'],parent_manifest_sha256=PARENT_MANIFEST_SHA,
            frozen_rule="Root-predeclared seven pairs, median >1.05 and bootstrap95 lower >1.0",trial_decision_sha256=DECISION_SHA,encoding_reference_sha256=ENCODING_REFERENCE_SHA,proposal_provenance_sha256=args.proposal_provenance_sha256,
            source_inventory_sha256=args.source_inventory_sha256,source_sha256=expected_sources,binaries_sha256=release,
            focused_receipt_sha256=args.focused_receipt_sha256,build_receipt_sha256=args.build_receipt_sha256,
            parent_source_sha256=manifest['source_snapshot_sha256'],parent_release_sha256=manifest['files_sha256'],
            frozen_original_parity_receipt_sha256=BODY_SHA,cpython_body_rerun=False,new_fixture_cases=new_cases,source_count=len(expected_sources),new_owned_targets=new_targets)
        for path,value in ((CHILD,CHILD_SHA),(BENCHMARK,BENCHMARK_SHA),(CP.parent/'Lib/pickle.py',PICKLE_SHA),(HOOK,HOOK_SHA),
            (CP,'4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9'),
            (CP.with_name('python314.dll'),'0f9857ffdfe010fe6b99328d58c2e3c7472ce75f336bf9c2ad9bd5bca3bce700')): pin(path,value)
        for name in ('io.py','datetime.py','random.py','copyreg.py','struct.py','_compat_pickle.py'): pin(CP.parent/'Lib'/name)
        for name in ('_datetime.pyd','_random.pyd','_struct.pyd','_hashlib.pyd'):
            path = CP.parent/'DLLs'/name
            if path.is_file(): pin(path)
        historical_path = DATA/'pyperformance-cpython3147-live-eval-full-fast-20261007-provenance.json'
        pin(historical_path,'3d6c8cb2c72c786583c382b835e8e411f56ca285963bc2835f757a13f399f85c')
        historical = read(historical_path); site = Path(historical['dependency_site']).resolve(strict=True)
        metadata = {p.relative_to(site).as_posix(): pin(p) for p in sorted(site.glob('*.dist-info/METADATA'))}
        assert metadata == {p.replace('\\','/'): v for p,v in historical['dependency_metadata_sha256'].items()}
        for package in (CP.parent/'Lib/site-packages/pyperf',site/'pyperf'):
            for path in sorted(package.rglob('*.py')): pin(path)
        record['controller_sha256'] = pin(__file__); record['child_sha256'] = CHILD_SHA
        environment = os.environ.copy()
        for name in ('PYTHONOPTIMIZE','PYTHONPATH','PYTHONPYCACHEPREFIX','PYTHONIOENCODING','_NT_SYMBOL_PATH','_NT_ALT_SYMBOL_PATH'): environment.pop(name,None)
        assert not environment.get('XLANG3_VM_OPCODE_TIMING')
        environment.update(XLANG3_PYTHON_LIB=str(CP.parent/'Lib'),PYTHONPATH=os.pathsep.join((str(HOOK.parent),str(site))),
            PYTHONIOENCODING='utf-8',PYTHONUNBUFFERED='1',PYTHONPYCACHEPREFIX=str(ROOT/'scratch/performance'/('pycache-'+args.prefix)))
        record['child_environment'] = {name:environment[name] for name in ('XLANG3_PYTHON_LIB','PYTHONPATH','PYTHONIOENCODING','PYTHONUNBUFFERED','PYTHONPYCACHEPREFIX')}
        pin(WATCH,WATCH_SHA)
        watch_spec=importlib.util.spec_from_file_location('pickle_original_pairs_watch',WATCH)
        watcher=importlib.util.module_from_spec(watch_spec); watch_spec.loader.exec_module(watcher)
        record['hashes_before']=dict(pins)
        executables={'parent':PARENT_RELEASE/'xlang3.exe','candidate':EXE}
        record['runtime_executables']={name:str(path) for name,path in executables.items()}
        record['timing_field']='original_timer_seconds_diagnostic_only'
        for index,order in enumerate(ORDERS):
            pair=dict(pair_index=index,order=list(order),runs={})
            record['pair_results'].append(pair); save()
            for role in order:
                label=f'pair-{index+1:02d}-{role}'; executable=executables[role]
                idle('before-'+label); assert stable()
                cache=ROOT/'scratch/performance'/('pycache-'+args.prefix+'-'+label)
                assert not cache.exists(), 'Each child must have a fresh distinct pycache prefix'
                child_env=dict(environment,PYTHONPYCACHEPREFIX=str(cache))
                stdout=DATA/(args.prefix+'-'+label+'.stdout.log'); stderr=DATA/(args.prefix+'-'+label+'.stderr.log')
                row=dict(runtime=role,pair_index=index,stdout_log=stdout.name,stderr_log=stderr.name,timeout=False,passed=False,
                    command=[str(executable),str(CHILD),'--benchmark-script',str(BENCHMARK)],pycache_prefix=str(cache))
                pair['runs'][role]=row; record['raw'].append(row); save(); child=None
                finish_watch=watcher.start_timing_process_watch(args.prefix,label,row)
                try:
                    with stdout.open('xb') as out,stderr.open('xb') as err:
                        child=subprocess.Popen(row['command'],cwd=ROOT,env=child_env,stdin=subprocess.DEVNULL,
                            stdout=out,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW)
                        row['pid']=child.pid; save()
                        try: row['exit_code']=child.wait(timeout=300)
                        except subprocess.TimeoutExpired:
                            row['timeout']=True
                            cleanup=subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)],capture_output=True,timeout=15)
                            row['timeout_cleanup_exit_code']=cleanup.returncode
                            row['timeout_cleanup_stdout']=cleanup.stdout.decode('utf-8',errors='replace')
                            row['timeout_cleanup_stderr']=cleanup.stderr.decode('utf-8',errors='replace')
                            if child.poll() is None: child.kill()
                            row['exit_code']=child.wait(timeout=15)
                    assert row['exit_code']==0 and not row['timeout']
                    event=validate_child(stdout,stderr,'xlang3',executable,row)
                    assert event['signature_after']==record['result_signature']
                    assert math.isfinite(row['original_timer_seconds_diagnostic_only']) and row['original_timer_seconds_diagnostic_only']>0
                    assert math.isfinite(row['elapsed_seconds_diagnostic_only']) and row['elapsed_seconds_diagnostic_only']>0
                    row['passed']=True
                except BaseException as error: row['error']=type(error).__name__+': '+str(error)
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
                idle('after-'+label); assert stable()
                assert row['passed'], 'Failed/invalid child retained; no retry or replacement sample'
            pair['ratio_parent_over_candidate']=pair['runs']['parent']['original_timer_seconds_diagnostic_only']/pair['runs']['candidate']['original_timer_seconds_diagnostic_only']
            save()
        idle('after-all-pairs'); assert stable()
        assert len(record['raw'])==PAIR_COUNT*2 and all(row['passed'] for row in record['raw'])
        ratios=[pair['ratio_parent_over_candidate'] for pair in record['pair_results']]
        assert len(ratios)==PAIR_COUNT and all(math.isfinite(value) and value>0 for value in ratios)
        rng=random.Random(BOOTSTRAP_SEED)
        draws=sorted(statistics.median([ratios[rng.randrange(PAIR_COUNT)] for _ in range(PAIR_COUNT)]) for _ in range(BOOTSTRAP_RESAMPLES))
        def percentile(probability):
            index=probability*(len(draws)-1); lower=math.floor(index); upper=math.ceil(index)
            return draws[lower]+(draws[upper]-draws[lower])*(index-lower)
        median=statistics.median(ratios); interval=[percentile(.025),percentile(.975)]
        useful=median>MINIMUM_MEDIAN_GAIN and interval[0]>MINIMUM_LOWER_CI
        record['summary']=dict(pair_ratios_parent_over_candidate=ratios,median_pair_ratio=median,bootstrap95_ci=interval,
            useful_signal=useful,decision='signal_for_further_validation_only' if useful else 'reject_candidate_retain_verified_parent',
            parent_seconds=[pair['runs']['parent']['original_timer_seconds_diagnostic_only'] for pair in record['pair_results']],
            candidate_seconds=[pair['runs']['candidate']['original_timer_seconds_diagnostic_only'] for pair in record['pair_results']],
            scope='Paired unscored original-body diagnostic; no official score, whole-suite claim or automatic acceptance/rollback',
            bootstrap_method='Resample seven paired ratios with replacement; median statistic; linear percentile endpoints; fixed seed/count')
        record['status']='terminal_unscored_paired_original_body_diagnostic'
    except BaseException as error:record.update(status='terminal_failed_diagnostic_controller',error=type(error).__name__+': '+str(error))
    finally:
        record['hashes_after']={p:sha(p) if Path(p).is_file() else None for p in pins}
        record['release_after']=release_map()
        record['hashes_unchanged']=stable(); record['input_verification_complete']=release is not None
        if not record['hashes_unchanged']:record['status']='terminal_invalid_hash_drift'
        record.update(terminal=True,completed_utc=datetime.now(timezone.utc).isoformat());save()
    return 0 if record['status']=='terminal_unscored_paired_original_body_diagnostic' else 1

if __name__=='__main__':raise SystemExit(main())
