"""L9 original pure-Python pickle parity/control body; never an acceptance signal.

CPython 3.14.7 is the manager. Body mode runs CP then the fixed candidate once. The sole permitted mode is body. S8 is an immutable historical control; new layout correctness is fresh. A neutral pickle control does not reject a useful callback/pprint improvement.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
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
PHASES = ['cpp', 'sorted7', 'iteration4', 'nested1', 'canonical10', 'fallback3', 'ownership2', 'owner2', 'namespace4', 'profile3', 'annotation2', 'threading', 'module_binding6', 'integer_sort7', 'generator_resume', 'debug_frames', 'monitoring']
HISTORICAL_S8_PHASES = ['sorted7', 'iteration4', 'nested1', 'canonical10', 'fallback3', 'ownership2', 'owner2', 'namespace4', 'profile3', 'annotation2', 'cpp', 'threading', 'module_binding6', 'integer_sort7']
HISTORICAL_C5_PHASES = ['sorted7', 'iteration4', 'nested1', 'canonical10', 'fallback3', 'ownership2', 'owner2', 'namespace4', 'profile3', 'annotation2', 'cpp']
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
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', required=True, choices=('body',))
    parser.add_argument('--source-inventory', type=Path, required=True)
    parser.add_argument('--source-inventory-sha256', required=True)
    parser.add_argument('--focused-receipt', type=Path, required=True)
    parser.add_argument('--focused-receipt-sha256', required=True)
    parser.add_argument('--body-receipt', type=Path)
    parser.add_argument('--body-receipt-sha256')
    parser.add_argument('--prefix', required=True)
    parser.add_argument('--build-log', type=Path, required=True)
    parser.add_argument('--build-log-sha256', required=True)
    args = parser.parse_args()
    assert sys.implementation.name == 'cpython' and sys.version_info[:3] == (3,14,7) and sys.flags.optimize == 0
    assert Path(sys.executable).resolve() == CP.resolve()
    assert re.fullmatch(r'[a-z0-9-]+', args.prefix) and not any(DATA.glob(args.prefix + '*'))
    assert bool(args.body_receipt) == bool(args.body_receipt_sha256)
    assert (args.mode == 'sample') == bool(args.body_receipt)
    output = DATA / (args.prefix + '.json')
    pins, release = {}, {}
    record = dict(status='preflight', terminal=False, mode=args.mode, diagnostic_only=True,
        scored=False, acceptance=False, full_validated=False, fixed_gate=None,
        scope='One original function invocation; no pyperf normalization, speed score, CP win or acceptance claim',
        outer_loops=41, protocol=5, original_dump_operations=2460, profile_enabled=False,
        timeout_seconds=300, idle_guards=[], raw=[], started_utc=datetime.now(timezone.utc).isoformat())
    def save(): output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))
    def pin(path, expected=None):
        path = Path(path).resolve(strict=True)
        value = sha(path)
        assert expected is None or value == expected, str(path)
        assert str(path) not in pins or pins[str(path)] == value
        pins[str(path)] = value
        return value
    def release_map(): return {p.relative_to(ROOT).as_posix(): sha(p) for p in sorted(RELEASE.rglob('*')) if p.is_file()}
    def stable(): return all(Path(p).is_file() and sha(p) == value for p,value in pins.items()) and release_map() == release
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
        focused_controller=ROOT/'scratch/performance/check-immutable-entry-layout-focused-r2-20261008.py'
        pin(focused_controller,'4b5d28ec1fd6ec59ebfbc9b7baa40f2c3b879e5f332da0cacf7ef0806fb42517')
        assert focus['controller_sha256']==sha(focused_controller)
        assert focus['source_inventory_sha256'] == args.source_inventory_sha256 and focus['source_sha256'] == inventory['source_sha256']
        assert [p['name'] for p in focus['phases']] == PHASES
        assert focus['hashes_before'] == focus['hashes_after']
        for path,value in focus['hashes_before'].items(): pin(Path(path),value)
        assert focus['build_log_sha256'] == args.build_log_sha256 and Path(focus['build_log']).resolve() == args.build_log.resolve()
        for row in focus['phases']:
            assert row['exit_code'] == 0 and not row.get('timeout',False) and row.get('passed',True)
            if row['name'] != 'cpp': assert row['output_matches_expected']
            for stream in ('stdout','stderr'): pin(DATA / row[stream + '_log'], row[stream + '_sha256'])
            if row['name'] != 'cpp': assert (DATA / row['stderr_log']).read_bytes() == b''
        # S8's complete source and Release bytes live in the immutable control.
        # Its manifest predates the timed pyflate case; do not upgrade that label.
        baseline_inventory_path=DATA/'sorted-exact-int-s8-registered-source-20261008.json'
        baseline_full_path=DATA/'sorted-exact-int-s8-full-validation-20261008.json'
        baseline_body_path=DATA/'pickle-sorted-exact-int-s8-body-20261008.json'
        baseline_focus_path=DATA/'sorted-exact-int-s8-registered-focused-20261008.json'
        baseline_inventory_sha='f20c304e32b0874c929d22dc72018cbd6d5fd9d124844c1d66d8edcc6a231556'
        baseline_full_sha='d4b028b1255f23b11e664bd9e6778b808a7d160d3628e17f39710faa1900d022'
        baseline_body_sha='7ee3b2b4644967d71dd83eaf634d3f7612858c2f798599127cfe96555fca6a3b'
        baseline_focus_sha='9fbe5bc1eb111876175ed630d0140ac6f0b9d1eb4292ac956778c81cd2fe4d5f'
        for path,value in ((baseline_inventory_path,baseline_inventory_sha),(baseline_full_path,baseline_full_sha),
            (baseline_body_path,baseline_body_sha),(baseline_focus_path,baseline_focus_sha)): pin(path,value)
        baseline_inventory=read(baseline_inventory_path); baseline_full=read(baseline_full_path)
        baseline_body=read(baseline_body_path); baseline_focus=read(baseline_focus_path)
        assert baseline_full['terminal'] and baseline_full['hashes_unchanged'] and baseline_full['release_tree_unchanged']
        assert baseline_full['status']=='validated' and baseline_full['correctness_passed'] and baseline_full['fixed_gate']['exit_code']==0
        assert baseline_full['source_inventory_sha256']==baseline_inventory_sha and baseline_full['source_sha256']==baseline_inventory['source_sha256']
        assert baseline_full['hashes_before']==baseline_full['hashes_after']
        gate_path=DATA/baseline_full['fixed_gate']['output']; pin(gate_path,baseline_full['fixed_gate']['sha256']); gate=read(gate_path)
        assert gate['status']=='pass' and len(gate['cases'])==11 and (gate['repeats'],gate['warmup'],gate['threshold'])==(21,5,.1)
        baseline_control=ROOT/'build-repro/controls/sorted-exact-int-s8-validated-checkpoint-20261008'
        manifest_path=baseline_control/'preserved-release-provenance.json'
        pin(manifest_path,'67449b0b9ecd5a1b4c9669acef85d8b89eb28c7b31df60b7a4653ad5a7c79b56'); control=read(manifest_path)
        assert control['status']=='preserved_correctness_gate_and_official_pickle_checkpoint_pyflate_pending'
        assert not control['pyflate_timed_attempt_complete'] and not control['accepted_gate_baseline_changed']
        assert control['validation_receipt_sha256']==baseline_full_sha and control['source_count']==110
        assert control['source_snapshot_sha256']==baseline_inventory['source_sha256']==baseline_full['source_sha256']
        snapshot=(baseline_control/'source-snapshot').resolve(strict=True)
        assert snapshot.is_relative_to(baseline_control.resolve()) and snapshot!=baseline_control.resolve()
        assert {p.relative_to(snapshot).as_posix():sha(p) for p in snapshot.rglob('*') if p.is_file()}==control['source_snapshot_sha256']
        for path,value in control['source_snapshot_sha256'].items(): pin(snapshot/path,value)
        control_files={p.relative_to(baseline_control).as_posix():sha(p) for p in baseline_control.rglob('*')
            if p.is_file() and p!=manifest_path and not p.is_relative_to(snapshot)}
        old_prefix='build-repro/main-verify-20261006/Release/'
        assert all(path.startswith(old_prefix) for path in baseline_full['binaries_sha256'])
        old_release={path[len(old_prefix):]:value for path,value in baseline_full['binaries_sha256'].items()}
        assert len(control_files)==control['file_count']==178 and control_files==control['files_sha256']==old_release
        for path,value in control_files.items(): pin(baseline_control/path,value)
        assert baseline_body['terminal'] and baseline_body['hashes_unchanged'] and baseline_body['hashes_before']==baseline_body['hashes_after']
        assert baseline_body['status']=='terminal_unscored_original_body_match' and baseline_body['mode']=='body'
        assert baseline_body['controller_sha256']=='a20405c6fdb266abf948f6d0c3c6fd07ff5748b5ee1d05ce266d29b02115d73d'
        assert baseline_body['source_sha256']==baseline_inventory['source_sha256'] and baseline_body['binaries_sha256']==baseline_full['binaries_sha256']
        assert baseline_body['source_inventory_sha256']==baseline_inventory_sha and baseline_body['focused_receipt_sha256']==baseline_focus_sha
        assert baseline_body['child_sha256']==CHILD_SHA and baseline_body['focused_phases']==HISTORICAL_S8_PHASES
        assert baseline_focus['terminal'] and baseline_focus['hashes_unchanged'] and baseline_focus['status']=='targeted_correctness_passed'
        assert len(baseline_body['raw'])==2 and all(row['passed'] and row['exit_code']==0 and not row['timeout'] for row in baseline_body['raw'])
        for row in baseline_body['raw']:
            for stream in ('stdout','stderr'): pin(DATA/row[stream+'_log'],row[stream+'_sha256'])
        layout_proof_path=ROOT/'scratch/performance/native-python-entry-immutable-layout-proposed-20261008-provenance.json'
        pin(layout_proof_path,'bde7e4fa9b891e60eef8886107675df34c0e6905b94259c59a6c2574b7e2e797'); layout_proof=read(layout_proof_path)
        expected_sources=dict(baseline_inventory['source_sha256']); expected_sources.update(layout_proof['candidate_source_sha256'])
        assert inventory['source_count']==len(inventory['source_sha256'])==111 and inventory['source_sha256']==expected_sources
        assert layout_proof['patch_sha256']=='f5a1cf0588369abe0ed15e007dbf34fb101eafabca6d50ac08b4df4a83950e83'
        pin(args.build_log,args.build_log_sha256)
        record['build_reference']=dict(path=str(args.build_log.resolve()),sha256=args.build_log_sha256,scope='Caller-supplied actual fixed-path build log; fresh CPP/focus separately pins built source/binaries')
        record['s8_parent_reference']=dict(receipt=str(baseline_full_path),sha256=baseline_full_sha,
            source_inventory_sha256=baseline_inventory_sha,control=str(baseline_control),control_manifest_sha256=sha(manifest_path),
            original_body=str(baseline_body_path),original_body_sha256=baseline_body_sha,
            scope='Immutable historical S8 checkpoint and single original pickle body; no changed-candidate correctness or timings reused')
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
        if args.mode == 'sample':
            pin(args.body_receipt,args.body_receipt_sha256); body = read(args.body_receipt)
            assert body['terminal'] and body['hashes_unchanged'] and body['status'] == 'terminal_unscored_original_body_match' and body['mode'] == 'body'
            assert body['controller_sha256'] == record['controller_sha256'] and body['child_sha256'] == CHILD_SHA
            assert body['source_sha256'] == inventory['source_sha256'] and body['binaries_sha256'] == release
            assert body['source_inventory_sha256'] == args.source_inventory_sha256 and body['focused_receipt_sha256'] == args.focused_receipt_sha256
            assert len(body['raw']) == 2 and all(row['passed'] for row in body['raw'])
            for row in body['raw']:
                for stream in ('stdout','stderr'): pin(DATA/row[stream+'_log'],row[stream+'_sha256'])
            record['body_reference'] = dict(path=str(args.body_receipt.resolve()),sha256=args.body_receipt_sha256)
            record['result_signature'] = body['result_signature']
            pin(SAMPLER,SAMPLER_SHA); pin(SAMPLER_SOURCE,SAMPLER_SOURCE_SHA); pin(SAMPLER_LOG,SAMPLER_LOG_SHA); pin(ANALYZER,ANALYZER_SHA)
            assert not SAMPLER.with_name('dbghelp.dll').exists()
            for name in ('ntdll.dll','ucrtbase.dll','vcruntime140.dll','kernel32.dll','kernelbase.dll','dbghelp.dll'):
                path=Path('C:/Windows/System32')/name
                if path.is_file(): pin(path)
        record['hashes_before'] = dict(pins)
        roles = [('cpython3147',CP),('candidate-xlang3',EXE)] if args.mode == 'body' else [('native-sampling',SAMPLER)]
        for label,executable in roles:
            idle('before-'+label); assert stable()
            stdout=DATA/(args.prefix+'-'+label+'.stdout.log'); stderr=DATA/(args.prefix+'-'+label+'.stderr.log')
            row=dict(runtime=label,stdout_log=stdout.name,stderr_log=stderr.name,timeout=False,passed=False)
            record['raw'].append(row)
            if args.mode == 'body':
                command=[str(executable),str(CHILD),'--benchmark-script',str(BENCHMARK)]
            else:
                childout=DATA/(args.prefix+'-child.stdout.log'); childerr=DATA/(args.prefix+'-child.stderr.log'); samplespath=DATA/(args.prefix+'-samples.jsonl')
                command=[str(SAMPLER),str(EXE),str(CHILD),str(BENCHMARK),str(childout),str(childerr),str(samplespath)]
                row['child_stdout_log']=childout.name; row['child_stderr_log']=childerr.name; row['samples_log']=samplespath.name
            row['command']=command; save(); child=None
            try:
                with stdout.open('xb') as out,stderr.open('xb') as err:
                    child=subprocess.Popen(command,cwd=ROOT,env=environment,stdin=subprocess.DEVNULL,stdout=out,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW)
                    row['pid']=child.pid; save()
                    try: row['exit_code']=child.wait(timeout=300 if args.mode=='body' else 325)
                    except subprocess.TimeoutExpired:
                        row['timeout']=True
                        if args.mode=='body': subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)],capture_output=True,timeout=15)
                        if child.poll() is None: child.kill()
                        row['exit_code']=child.wait(timeout=15)
                assert row['exit_code']==0 and not row['timeout']
                if args.mode=='body':
                    event=validate_child(stdout,stderr,'cpython' if label=='cpython3147' else 'xlang3',executable,row)
                    if label=='cpython3147': record['result_signature']=event['signature_after']
                    assert event['signature_after']==record['result_signature']; row['passed']=True
                else:
                    assert stderr.read_bytes()==b''
                    event=validate_child(childout,childerr,'xlang3',EXE,row)
                    assert event['signature_after']==record['result_signature']
                    rows=[json.loads(line) for line in samplespath.read_text(encoding='utf-8').splitlines() if line.strip()]
                    terminal=[r for r in rows if r['event']=='terminal']
                    assert len(terminal)==1 and rows[-1]==terminal[0]
                    terminal=terminal[0]
                    assert terminal['child_exit_code']==0 and not terminal['timeout'] and terminal['start_seen'] and terminal['end_seen']
                    samples=[r for r in rows if r['event']=='sample']
                    assert samples and len(samples)==terminal['sample_count']
                    record['sampler_terminal']=terminal; record['sample_count']=len(samples)
                    modules={m['base']:m for r in rows if r['event']=='modules' for m in r['items']}
                    spec=importlib.util.spec_from_file_location('pickle_pe_attribution',ANALYZER)
                    analyzer=importlib.util.module_from_spec(spec); spec.loader.exec_module(analyzer); decode=analyzer.demangler()
                    images,labels={},{}
                    def locate(pc):
                        for module in modules.values():
                            if module['base']<=pc<module['base']+module['size']:
                                path=Path(module['path']).resolve(); rva=pc-module['base']; key=(str(path),rva)
                                if key in labels: return labels[key]
                                if str(path) not in pins: result=(path.name+':unproved-image:'+hex(rva),[],'image_not_before_after_pinned')
                                else:
                                    if str(path) not in images:
                                        try: images[str(path)]=analyzer.PE(path)
                                        except Exception: images[str(path)]=None
                                    image=images[str(path)]; function=image.containing(rva) if image else None; anchored=[]; anchor=None
                                    for candidate in image.chain(function) if function else []:
                                        anchored=image.export_names_in(candidate)
                                        if anchored: anchor=candidate; break
                                    if anchored: result=(path.name+':range:'+hex(anchor[0]),sorted({decode(name) for entry,name in anchored}),'export_in_containing_or_chained_pdata')
                                    elif image and rva in image.exports: result=(path.name+':entry:'+hex(rva),sorted({decode(n) for n in image.exports[rva]}),'exact_export_entry')
                                    else: result=(path.name+':unresolved:'+hex(function[0] if function else rva),[],'unresolved_no_exact_range_anchor')
                                labels[key]=result; return result
                        key=('unknown',pc); labels[key]=('unknown-module:'+hex(pc),[],'unknown_module'); return labels[key]
                    exclusive,inclusive=Counter(),Counter()
                    for sample in samples:
                        exclusive[locate(sample['pcs'][0])[0]]+=1
                        groups={locate(pc if index==0 else max(0,pc-1))[0] for index,pc in enumerate(sample['pcs'])}
                        inclusive.update(groups)
                    labelmap={v[0]:v for v in labels.values()}
                    def groups(counter): return [dict(group=k,samples=n,fraction_samples=n/len(samples),names=labelmap[k][1],evidence=labelmap[k][2]) for k,n in counter.most_common()]
                    record['exclusive_leaf_groups']=groups(exclusive); record['inclusive_stack_groups']=groups(inclusive)
                    record['sampling_limits']=['Elapsed time is perturbed and never scored','Leaf locations include inlined work; inclusive stacks overlap and cannot be added',
                        'Polling selects the largest CPU-delta thread, not exact CPU accounting','Start excludes imports; end follows three output audit dumps/loads and reporting',
                        'Only exact current pinned image ranges/export anchors are labelled; unknown PCs remain unresolved','Caller return PCs use PC-1; no Debug PDB or nearest-export attribution']
                    row['passed']=True
            except BaseException as error: row['error']=type(error).__name__+': '+str(error)
            finally:
                if child is not None and child.poll() is None: child.kill(); child.wait(timeout=15)
                for stream,path in (('stdout',stdout),('stderr',stderr)):
                    row[stream+'_sha256']=sha(path) if path.is_file() else None
                if args.mode=='sample':
                    for name,path in (('child_stdout',childout),('child_stderr',childerr),('samples',samplespath)):
                        row[name+'_sha256']=sha(path) if path.is_file() else None
                save()
            assert stable()
        idle('after-children-and-analysis'); assert stable()
        if args.mode=='body' and all(row['passed'] for row in record['raw']):
            assert record['result_signature']==baseline_body['result_signature']
            old_rows={row['runtime']:row for row in baseline_body['raw']}
            new_rows={row['runtime']:row for row in record['raw']}
            old_x=old_rows['candidate-xlang3']['elapsed_seconds_diagnostic_only']
            new_x=new_rows['candidate-xlang3']['elapsed_seconds_diagnostic_only']
            record['unscored_s8_control_comparison']=dict(s8_x_seconds=old_x,candidate_x_seconds=new_x,s8_over_candidate=old_x/new_x,
                s8_cp_seconds=old_rows['cpython3147']['elapsed_seconds_diagnostic_only'],
                candidate_cp_seconds=new_rows['cpython3147']['elapsed_seconds_diagnostic_only'],
                scope='Single original-body S8 versus fresh layout candidate pickle control; unpaired/unscored, no threshold/acceptance/CP-win claim; neutral pickle does not reject affected pprint/callback improvement')
        record['status']=('terminal_unscored_original_body_match' if args.mode=='body' else 'terminal_unscored_native_location_diagnostic') if all(r['passed'] for r in record['raw']) else 'terminal_failed_diagnostic_or_output_mismatch'
    except BaseException as error: record.update(status='terminal_failed_diagnostic_controller',error=type(error).__name__+': '+str(error))
    finally:
        record['hashes_after']={p:sha(p) if Path(p).is_file() else None for p in pins}
        record['release_after']=release_map()
        record['hashes_unchanged']=record['hashes_after']==pins and record['release_after']==release
        if not record['hashes_unchanged']: record['status']='terminal_invalid_hash_drift'
        record['terminal']=True; record['completed_utc']=datetime.now(timezone.utc).isoformat(); save()
    print('Original pickle control diagnostic terminal:',record['status'],flush=True)
    return 0 if record['status'] in ('terminal_unscored_original_body_match','terminal_unscored_native_location_diagnostic') else 1

if __name__=='__main__': raise SystemExit(main())
