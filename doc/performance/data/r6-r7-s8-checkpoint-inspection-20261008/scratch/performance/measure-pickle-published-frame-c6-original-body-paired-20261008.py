"""Seven predeclared alternating original pure-Python pickle body pairs.

Preserved C5 versus fixed R6, one unchanged 2,460-dump invocation per child.
CPython3.14.7 manages only; no CP timed child, retries, trimming or count changes.
This is an unscored paired diagnostic, not an official result or CP win.
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

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
EXE = RELEASE / 'xlang3.exe'
CHILD = ROOT / 'scratch/performance/pickle-original-pure-python-diagnostic-proposed-20261008.py'
CHILD_SHA = '5295c899d95d023ca2fb3b511e9bf78b58cb49e4a256770c65adf0d614b14109'
BENCHMARK = CP.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks/bm_pickle/run_benchmark.py'
BENCHMARK_SHA = '31c0e30be79514b45db0d4e858632eb429b0fea3edbac6463ad5f34d92f3cab8'
PICKLE_SHA = '144fdf593814627e13a5fcb0947728e8f0775ecdec181046ee4609063cf232ec'
HOOK = ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
HOOK_SHA = '2ffc217fe20497156bfcb1cebc7fb87203f33c6d4d3dbb6ecbcbcf91ea4cb317'
PHASES = ['sorted7', 'iteration4', 'nested1', 'canonical10', 'fallback3', 'ownership2', 'owner2', 'namespace4', 'profile3', 'annotation2', 'cpp', 'threading']
C5_PARENT = DATA / 'ordinary-canonical-slot-constructor-c5-gate-idle-resume-20261008.json'
C5_PARENT_SHA = 'a3be06b1ef97560253e7f4f0d7740440786f82ca08e0a56b564bfe2bfd3f96ed'
C5_INVENTORY = DATA / 'class-constructor-plan-c5-applied-source-20261008.json'
C5_INVENTORY_SHA = '69aaf03539202bbc7fced4dfe46df5f3a801a6d40b8e0360a68245de6ce71937'
C5_BODY = DATA / 'pickle-original-pure-c5-body-20261008.json'
C5_BODY_SHA = '2d6a79f263d3e20f86f494bc13dfef795e395fba0b81773a2bc238472d811665'
C5_SAMPLE = DATA / 'pickle-original-pure-c5-native-sampling-20261008.json'
C5_SAMPLE_SHA = 'e5919bc0ec2840d3300888f1fb54dd10ac66cb96a66734d47a05322d26f5a0a7'
C5_CONTROL = ROOT / 'build-repro/controls/class-constructor-c5-before-snapshot-r6-20261008'
C5_CONTROL_MANIFEST_SHA = '85910ee375f16c677153fc128fb17303868a849c124f5909d5ee03eeed764bdc'
THREAD_SOURCE = ROOT / 'tests/fixtures/core/threading_runtime_edges.py'
THREAD_SOURCE_SHA = '56016bd84f76782ce736058670c50035c8a1600ac67fca201ef1a304e6875c39'
THREAD_EXPECTED = ROOT / 'tests/fixtures/expected/threading_runtime_edges.out'
THREAD_EXPECTED_SHA = '7ac4a64a76bc6207024ea9c001dcfcf336e991effe39b9c08602ceb7c332ba22'
SNAPSHOT_HEADER = 'tests/cpp/published_frame_snapshot_cases.h'
SAMPLER = ROOT / 'scratch/performance/sample-pprint-native-cpu-20261008.exe'
SAMPLER_SHA = '754d9bd7735fbd5f0d3d93f383b4b00ebd052e44eaa74174f2850531072cfa12'
SAMPLER_SOURCE = ROOT / 'scratch/performance/sample-pprint-native-cpu-proposed-20261008.cpp'
SAMPLER_SOURCE_SHA = '00571bcb8c06b72ed98ad085283dcf1a7389f91ebc7bdaa4fba2af33aa65d0a7'
SAMPLER_LOG = ROOT / 'scratch/performance/sample-pprint-native-cpu-build-20261008.log'
SAMPLER_LOG_SHA = 'b17eabbf197e998e6387e3c8f0fcafb758139f2f6f147c85779efc74891d933c'
ANALYZER = ROOT / 'scratch/performance/attribute-sqlglot-native-exports-20261008.py'
ANALYZER_SHA = 'e535c5d629e5fb4bfc1a360f7b730df0a9d13de86c6231c92e39244b1aa7920e'
INITIAL_BODY = DATA / 'pickle-published-frame-c6-body-20261008.json'
INITIAL_BODY_SHA = '0040315f22ccadd781ef6baaa8472b31765d4e6636bccaa9feec2121d58c1db6'
R6_INVENTORY_SHA = 'e1a655b96ec2931b607ddb4b037784ffcb069f814267ca0191871839d39f5221'
R6_FOCUS_SHA = 'b9eb06e0120d5474a0ef69b65a23da026807c81b3ef803487baf02c3a0eba253'
BODY_CONTROLLER = ROOT / 'scratch/performance/run-pickle-published-frame-c6-original-body-and-sampling-20261008.py'
BODY_CONTROLLER_SHA = 'd3d10bbf61edfaeaf92721952a18919ab1e8017e842051e933b4a0b893a8b699'
WATCH = ROOT / 'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'
WATCH_SHA = '50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf'
PAIR_COUNT = 7
ORDERS = [('control','candidate') if index % 2 == 0 else ('candidate','control') for index in range(PAIR_COUNT)]
BOOTSTRAP_RESAMPLES = 50000
BOOTSTRAP_SEED = 20261008
MINIMUM_MEDIAN_GAIN = 1.02
MINIMUM_LOWER_CI = 1.0
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-inventory', type=Path, required=True)
    parser.add_argument('--source-inventory-sha256', required=True)
    parser.add_argument('--focused-receipt', type=Path, required=True)
    parser.add_argument('--focused-receipt-sha256', required=True)
    parser.add_argument('--body-receipt', type=Path, default=INITIAL_BODY)
    parser.add_argument('--body-receipt-sha256', default=INITIAL_BODY_SHA)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    assert sys.implementation.name == 'cpython' and sys.version_info[:3] == (3,14,7) and sys.flags.optimize == 0
    assert Path(sys.executable).resolve() == CP.resolve()
    assert re.fullmatch(r'[a-z0-9-]+', args.prefix) and not any(DATA.glob(args.prefix + '*'))
    assert args.source_inventory_sha256==R6_INVENTORY_SHA and args.focused_receipt_sha256==R6_FOCUS_SHA
    assert args.body_receipt.resolve()==INITIAL_BODY.resolve() and args.body_receipt_sha256==INITIAL_BODY_SHA
    output = DATA / (args.prefix + '.json')
    pins, release = {}, {}
    record = dict(status='preflight', terminal=False, mode='paired-original-body', diagnostic_only=True,
        scored=False, acceptance=False, full_validated=False, fixed_gate=None,
        scope='Seven alternating original-body C5/R6 pairs; no pyperf normalization, official score, CP child or acceptance claim',
        outer_loops=41, protocol=5, original_dump_operations=2460, profile_enabled=False,
        timeout_seconds=300, idle_guards=[], raw=[], pair_results=[], pair_count=PAIR_COUNT, pair_order=ORDERS, total_body_invocations=PAIR_COUNT*2, total_dump_operations=PAIR_COUNT*2*2460,
        decision_rule=dict(median_ratio_strictly_greater_than=MINIMUM_MEDIAN_GAIN,lower_ci_strictly_greater_than=MINIMUM_LOWER_CI,
            ratio='median of seven within-pair C5 original timer / R6 original timer',bootstrap_resamples=BOOTSTRAP_RESAMPLES,
            bootstrap_seed=BOOTSTRAP_SEED,confidence_percent=95), values_trimmed=0,outliers_removed=0, started_utc=datetime.now(timezone.utc).isoformat())
    def save(): output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))
    def pin(path, expected=None):
        path = Path(path).resolve(strict=True)
        value = sha(path)
        assert expected is None or value == expected, str(path)
        assert str(path) not in pins or pins[str(path)] == value
        pins[str(path)] = value
        return value
    def release_map(): return {p.relative_to(ROOT).as_posix(): sha(p) for p in sorted(RELEASE.rglob('*')) if p.is_file()}
    def control_maps():
        snapshot_root=C5_CONTROL/'source-snapshot'
        release_rows={p.relative_to(C5_CONTROL).as_posix():sha(p) for p in C5_CONTROL.rglob('*')
            if p.is_file() and p!=C5_CONTROL/'preserved-release-provenance.json' and not p.is_relative_to(snapshot_root)}
        source_rows={p.relative_to(snapshot_root).as_posix():sha(p) for p in snapshot_root.rglob('*') if p.is_file()}
        return release_rows,source_rows
    def stable():
        old_release,old_sources=control_maps()
        return (all(Path(p).is_file() and sha(p)==value for p,value in pins.items()) and release_map()==release
            and old_release==record['control_release_sha256'] and old_sources==record['control_source_snapshot_sha256'])
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
        inventory = read(args.source_inventory); focus = read(args.focused_receipt)
        pin(args.source_inventory, args.source_inventory_sha256); pin(args.focused_receipt, args.focused_receipt_sha256)
        assert focus['terminal'] and focus['hashes_unchanged'] and focus['status'] == 'targeted_correctness_passed'
        assert focus['source_inventory_sha256'] == args.source_inventory_sha256 and focus['source_sha256'] == inventory['source_sha256']
        assert len(focus['phases']) == len(PHASES) and {p['name'] for p in focus['phases']} == set(PHASES)
        for row in focus['phases']:
            assert row['exit_code'] == 0 and not row.get('timeout',False) and row.get('passed',True)
            if row['name'] != 'cpp': assert row['output_matches_expected']
            for stream in ('stdout','stderr'): pin(DATA / row[stream + '_log'], row[stream + '_sha256'])
            if row['name'] != 'cpp': assert (DATA / row['stderr_log']).read_bytes() == b''
        # Historical C5 owners live in preserved copies, never in changed C6 files.
        for path, expected in ((C5_PARENT,C5_PARENT_SHA),(C5_INVENTORY,C5_INVENTORY_SHA),
            (C5_BODY,C5_BODY_SHA),(C5_SAMPLE,C5_SAMPLE_SHA)):
            pin(path,expected)
        parent, old_inventory, c5_body, c5_sample = map(read,(C5_PARENT,C5_INVENTORY,C5_BODY,C5_SAMPLE))
        assert parent['terminal'] and parent['hashes_unchanged'] and parent['release_tree_unchanged']
        assert parent['status']=='correctness_and_gate_passed_official_failed' and parent['correctness_passed'] and not parent['full_validated']
        assert parent['source_inventory_sha256']==C5_INVENTORY_SHA and parent['source_sha256']==old_inventory['source_sha256']
        assert parent['hashes_before']==parent['hashes_after'] and parent['fixed_gate']['exit_code']==0
        gate_path=DATA/parent['fixed_gate']['output']; pin(gate_path,parent['fixed_gate']['sha256']); gate=read(gate_path)
        assert gate['status']=='pass' and len(gate['cases'])==11 and (gate['repeats'],gate['warmup'],gate['threshold'])==(21,5,.1)
        manifest_path=C5_CONTROL/'preserved-release-provenance.json'
        pin(manifest_path,C5_CONTROL_MANIFEST_SHA); control=read(manifest_path)
        assert control['checkpoint']=='e6913f8fa33eb0ba0d452f451808ec9df3d5ff4d'
        assert control['status']=='preserved_committed_correctness_and_gate_checkpoint_official_timeout'
        assert control['source_inventory_sha256']==C5_INVENTORY_SHA and control['validation_receipt_sha256']==C5_PARENT_SHA
        assert control['source_snapshot_sha256']==parent['source_sha256'] and control['source_count']==106
        snapshot=(C5_CONTROL/control['source_snapshot_root']).resolve(strict=True)
        assert snapshot.is_relative_to(C5_CONTROL.resolve()) and snapshot!=C5_CONTROL.resolve()
        assert {p.relative_to(snapshot).as_posix():sha(p) for p in snapshot.rglob('*') if p.is_file()}==control['source_snapshot_sha256']
        for path,value in control['source_snapshot_sha256'].items(): pin(snapshot/path,value)
        control_files={p.relative_to(C5_CONTROL).as_posix():sha(p) for p in C5_CONTROL.rglob('*')
            if p.is_file() and p!=manifest_path and not p.is_relative_to(snapshot)}
        old_prefix='build-repro/main-verify-20261006/Release/'
        assert all(path.startswith(old_prefix) for path in parent['binaries_sha256'])
        old_release={path[len(old_prefix):]:value for path,value in parent['binaries_sha256'].items()}
        assert len(control_files)==control['file_count']==178 and control_files==control['files_sha256']==old_release
        for path,value in control_files.items(): pin(C5_CONTROL/path,value)
        record['control_release_sha256']=control_files
        record['control_source_snapshot_sha256']=control['source_snapshot_sha256']
        for receipt in (c5_body,c5_sample):
            assert receipt['terminal'] and receipt['hashes_unchanged'] and receipt['hashes_before']==receipt['hashes_after']
            assert receipt['source_sha256']==parent['source_sha256'] and receipt['binaries_sha256']==parent['binaries_sha256']
            assert receipt['source_inventory_sha256']==C5_INVENTORY_SHA and receipt['child_sha256']==CHILD_SHA
            assert receipt['controller_sha256']=='da328bd7d8167795c7ea23bf46d7e1577acbbcb4cddebe8c19909d6c0c6cc9d6'
            assert receipt['focused_phases']==PHASES[:-1]
        assert c5_body['status']=='terminal_unscored_original_body_match' and c5_body['mode']=='body'
        assert len(c5_body['raw'])==2 and all(row['passed'] and row['exit_code']==0 and not row['timeout'] for row in c5_body['raw'])
        assert c5_sample['status']=='terminal_unscored_native_location_diagnostic' and c5_sample['sample_count']==190
        assert c5_sample['body_reference']['sha256']==C5_BODY_SHA and c5_sample['mode']=='sample'
        assert c5_body['result_signature']==c5_sample['result_signature']
        for receipt, key in ((parent,'phases'),(c5_body,'raw'),(c5_sample,'raw')):
            for row in receipt[key]:
                for stream in ('stdout','stderr','child_stdout','child_stderr','samples'):
                    if stream+'_log' in row: pin(DATA/row[stream+'_log'],row[stream+'_sha256'])
        record['c5_parent_reference']=dict(receipt=str(C5_PARENT),sha256=C5_PARENT_SHA,source_inventory_sha256=C5_INVENTORY_SHA,
            control=str(C5_CONTROL),control_manifest_sha256=C5_CONTROL_MANIFEST_SHA,
            original_body=str(C5_BODY),original_body_sha256=C5_BODY_SHA,native_sample=str(C5_SAMPLE),native_sample_sha256=C5_SAMPLE_SHA,
            scope='Immutable historical C5 correctness/gate checkpoint with failed pprint official; no C5 correctness reused for changed C6 engine')
        assert SNAPSHOT_HEADER in inventory['source_sha256']
        cpp_text=(ROOT/'tests/cpp/interpreter_tests.cpp').read_text(encoding='utf-8')
        assert cpp_text.count('#include "published_frame_snapshot_cases.h"')==1
        assert cpp_text.count('xlang3::test::check_published_frame_same_owner_refresh(result);')==1
        pin(THREAD_SOURCE,THREAD_SOURCE_SHA); pin(THREAD_EXPECTED,THREAD_EXPECTED_SHA)
        release = release_map()
        assert len(release) == 178 and release == focus['binaries_sha256']
        assert sha(EXE) == focus['candidate_binary_sha256']['exe'] and sha(EXE.with_name('xlang3_runtime.dll')) == focus['candidate_binary_sha256']['dll']
        for path,value in {**inventory['source_sha256'], **release}.items(): pin(ROOT / path,value)
        thread_row=next(row for row in focus['phases'] if row['name']=='threading')
        assert thread_row['command']==[str(EXE),str(THREAD_SOURCE)]
        assert (DATA/thread_row['stdout_log']).read_bytes().replace(b'\r\n',b'\n')==THREAD_EXPECTED.read_bytes().replace(b'\r\n',b'\n')
        record.update(source_inventory=str(args.source_inventory.resolve()), source_inventory_sha256=args.source_inventory_sha256,
            focused_receipt=str(args.focused_receipt.resolve()), focused_receipt_sha256=args.focused_receipt_sha256,
            source_sha256=inventory['source_sha256'], binaries_sha256=release, release_count=len(release),
            candidate_binary_sha256=focus['candidate_binary_sha256'], focused_phases=PHASES)
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
        # Successful initial C6 CP/X parity is retained, never rerun here.
        pin(BODY_CONTROLLER,BODY_CONTROLLER_SHA); pin(args.body_receipt,INITIAL_BODY_SHA)
        initial=read(args.body_receipt)
        assert initial['terminal'] and initial['hashes_unchanged'] and initial['status']=='terminal_unscored_original_body_match'
        assert initial['mode']=='body' and initial['controller_sha256']==BODY_CONTROLLER_SHA and initial['child_sha256']==CHILD_SHA
        assert initial['source_sha256']==inventory['source_sha256'] and initial['binaries_sha256']==release
        assert initial['source_inventory_sha256']==R6_INVENTORY_SHA and initial['focused_receipt_sha256']==R6_FOCUS_SHA
        assert initial['hashes_before']==initial['hashes_after'] and initial['result_signature']==c5_body['result_signature']
        assert len(initial['raw'])==2 and all(row['passed'] and row['exit_code']==0 and not row['timeout'] for row in initial['raw'])
        for path,value in initial['hashes_before'].items(): pin(path,value)
        for row in initial['raw']:
            for stream in ('stdout','stderr'): pin(DATA/row[stream+'_log'],row[stream+'_sha256'])
        record['initial_body_reference']=dict(path=str(args.body_receipt.resolve()),sha256=INITIAL_BODY_SHA,cpython_child_rerun=False)
        record['result_signature']=c5_body['result_signature']
        pin(WATCH,WATCH_SHA)
        watch_spec=importlib.util.spec_from_file_location('pickle_original_pairs_watch',WATCH)
        watcher=importlib.util.module_from_spec(watch_spec); watch_spec.loader.exec_module(watcher)
        record['hashes_before']=dict(pins)
        executables={'control':C5_CONTROL/'xlang3.exe','candidate':EXE}
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
                    if child is not None and child.poll() is None:
                        subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)],capture_output=True,timeout=15)
                        if child.poll() is None: child.kill()
                        child.wait(timeout=15)
                    for stream,path in (('stdout',stdout),('stderr',stderr)):
                        row[stream+'_sha256']=sha(path) if path.is_file() else None
                    row['measurement_valid']=finish_watch()
                    row['passed']=row['passed'] and row['measurement_valid']
                    save()
                idle('after-'+label); assert stable()
                assert row['passed'], 'Failed/invalid child retained; no retry or replacement sample'
            pair['ratio_c5_over_r6']=pair['runs']['control']['original_timer_seconds_diagnostic_only']/pair['runs']['candidate']['original_timer_seconds_diagnostic_only']
            save()
        idle('after-all-pairs'); assert stable()
        assert len(record['raw'])==PAIR_COUNT*2 and all(row['passed'] for row in record['raw'])
        ratios=[pair['ratio_c5_over_r6'] for pair in record['pair_results']]
        assert len(ratios)==PAIR_COUNT and all(math.isfinite(value) and value>0 for value in ratios)
        rng=random.Random(BOOTSTRAP_SEED)
        draws=sorted(statistics.median([ratios[rng.randrange(PAIR_COUNT)] for _ in range(PAIR_COUNT)]) for _ in range(BOOTSTRAP_RESAMPLES))
        def percentile(probability):
            index=probability*(len(draws)-1); lower=math.floor(index); upper=math.ceil(index)
            return draws[lower]+(draws[upper]-draws[lower])*(index-lower)
        median=statistics.median(ratios); interval=[percentile(.025),percentile(.975)]
        useful=median>MINIMUM_MEDIAN_GAIN and interval[0]>MINIMUM_LOWER_CI
        record['summary']=dict(pair_ratios_c5_over_r6=ratios,median_pair_ratio=median,bootstrap95_ci=interval,
            useful_signal=useful,decision='signal_for_further_validation_only' if useful else 'reject_c6_retain_verified_c5',
            control_seconds=[pair['runs']['control']['original_timer_seconds_diagnostic_only'] for pair in record['pair_results']],
            candidate_seconds=[pair['runs']['candidate']['original_timer_seconds_diagnostic_only'] for pair in record['pair_results']],
            scope='Paired unscored original-body diagnostic; no official score, whole-suite claim or automatic acceptance/rollback',
            bootstrap_method='Resample seven paired ratios with replacement; median statistic; linear percentile endpoints; fixed seed/count')
        record['status']='terminal_unscored_paired_original_body_diagnostic'
    except BaseException as error: record.update(status='terminal_failed_diagnostic_controller',error=type(error).__name__+': '+str(error))
    finally:
        record['hashes_after']={p:sha(p) if Path(p).is_file() else None for p in pins}
        record['release_after']=release_map()
        record['control_release_after'],record['control_source_snapshot_after']=control_maps()
        record['hashes_unchanged']=(record['hashes_after']==pins and record['release_after']==release
            and record['control_release_after']==record.get('control_release_sha256')
            and record['control_source_snapshot_after']==record.get('control_source_snapshot_sha256'))
        if not record['hashes_unchanged']: record['status']='terminal_invalid_hash_drift'
        record['terminal']=True; record['completed_utc']=datetime.now(timezone.utc).isoformat(); save()
    print('Paired original pickle diagnostic terminal:',record['status'],flush=True)
    return 0 if record['status']=='terminal_unscored_paired_original_body_diagnostic' else 1

if __name__=='__main__': raise SystemExit(main())
