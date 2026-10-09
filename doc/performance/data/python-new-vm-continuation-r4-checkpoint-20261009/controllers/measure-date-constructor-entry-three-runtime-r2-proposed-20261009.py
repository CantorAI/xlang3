# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0.
"""Root-only watched CP/R5/candidate constructor-entry diagnostic; no suite score."""
import argparse
import hashlib
import importlib.util
import itertools
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BASELINE = ROOT / 'build-repro/Release'
CONTROL_MANIFEST = ROOT / 'build-repro/controls/lambda-eager-comprehension-capture-accepted-r5-20261009/preserved-release-provenance.json'
CONTROL_SHA = '9271314856ede7f173396b2f39449a01d785f61a1579ce5f93c7786a2961dfad'
ACCEPTED_BASE = 'b188b24e86eac9cbc10c7f0efe6234d98caa0009'
VALIDATION = DATA / 'lambda-eager-comprehension-capture-r5-validation-r5-20261009.json'
VALIDATION_SHA = '6a41ee3fe92453d6472f2bb1c24b2da7ef125ac8425d27efdffa9f2262ab2d35'
ORIGINAL_CHILD = ROOT / 'scratch/performance/date-constructor-entry-child-proposed-20261009.py'
ORIGINAL_CHILD_SHA = '37f9afee6cc5051efee02996f59f99fa6801aadfe93afb5033218b40859bf4b6'
ORIGINAL_MANAGER = ROOT / 'scratch/performance/measure-date-constructor-entry-balanced-proposed-20261009.py'
ORIGINAL_MANAGER_SHA = '14eac8ec2dc7b4ed903819b5b623f7b26eafb0385c35bfe3cf90334a58926284'
ORIGINAL_PROOF = ROOT / 'scratch/performance/date-constructor-entry-balanced-provenance-proposed-20261009.json'
ORIGINAL_PROOF_SHA = '556e5697ac4bef0eb5dfc90176f64fad26caeb5430b7c460e9d6d0440d64547e'
SOURCE = ROOT / 'scratch/performance/date-constructor-entry-three-runtime-child-proposed-20261009.py'
SOURCE_SHA = '19057739ba879cf4b01ca506aa69b01e3dc3f4a9d39c70b6641286984a390462'
PROOF = ROOT / 'scratch/performance/date-constructor-entry-three-runtime-r2-provenance-proposed-20261009.json'
WATCH = ROOT / 'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'
WATCH_SHA = '50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf'
SELECTIONS = ('cpython-python', 'xlang3-control', 'xlang3-candidate')
CP_PINS = {str(CP): '4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9',
           str(CP.with_name('python314.dll')): '0f9857ffdfe010fe6b99328d58c2e3c7472ce75f336bf9c2ad9bd5bca3bce700'}
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}

def maybe_sha(path):
    try:
        return sha(path)
    except OSError:
        return None

def read(path, expected):
    assert re.fullmatch(r'[0-9a-f]{64}', expected) and sha(path) == expected
    return json.loads(Path(path).read_bytes())

def normalize_release(mapping):
    """Accept existing root- or Release-relative receipt keys without aliases."""
    normalized = {}
    for name, value in mapping.items():
        relative = Path(name)
        assert not relative.is_absolute() and '..' not in relative.parts
        assert re.fullmatch(r'[0-9a-f]{64}', value)
        repo_path = (ROOT / relative).resolve()
        path = repo_path if repo_path.is_relative_to(RELEASE.resolve()) else (RELEASE / relative).resolve()
        assert path.is_relative_to(RELEASE.resolve())
        key = path.relative_to(RELEASE.resolve()).as_posix()
        assert key not in normalized
        normalized[key] = value
    return normalized

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--proof-sha256', required=True)
parser.add_argument('--applied-source', type=Path, required=True)
parser.add_argument('--applied-source-sha256', required=True)
parser.add_argument('--build-receipt', type=Path, required=True)
parser.add_argument('--build-receipt-sha256', required=True)
parser.add_argument('--focused-receipt', type=Path, required=True)
parser.add_argument('--focused-receipt-sha256', required=True)
parser.add_argument('--head', required=True, help='Exact actual 40-hex HEAD supplied by root')
parser.add_argument('--prefix', default='date-constructor-entry-cp-r5-r4-20261009')
args = parser.parse_args()
assert re.fullmatch(r'[a-z0-9-]+', args.prefix) and re.fullmatch(r'[0-9a-f]{40}', args.head)
PREFIX = args.prefix
OUT = DATA / (PREFIX + '.json')
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7)
assert sys.flags.isolated and not sys.flags.optimize and Path.cwd().resolve() == ROOT
assert not any(DATA.glob(PREFIX+'*')), 'Preserve every earlier attempt; no retry/overwrite'
proof = read(PROOF, args.proof_sha256)
assert proof['controller_sha256'] == sha(Path(__file__)) and proof['child_sha256'] == SOURCE_SHA
original = read(ORIGINAL_PROOF, ORIGINAL_PROOF_SHA)
assert sha(ORIGINAL_CHILD) == ORIGINAL_CHILD_SHA and sha(ORIGINAL_MANAGER) == ORIGINAL_MANAGER_SHA
assert sha(SOURCE) == SOURCE_SHA and sha(WATCH) == WATCH_SHA
applied_path, build_path, focused_path = (p.resolve(strict=True) for p in (args.applied_source, args.build_receipt, args.focused_receipt))
assert all(p.is_relative_to(DATA.resolve()) for p in (applied_path, build_path, focused_path))
assert args.applied_source_sha256 == proof['applied_source_sha256']
assert args.build_receipt_sha256 == proof['build_receipt_sha256']
assert args.focused_receipt_sha256 == proof['focused_receipt_sha256']
applied = read(applied_path, args.applied_source_sha256)
build = read(build_path, args.build_receipt_sha256)
focused = read(focused_path, args.focused_receipt_sha256)
assert applied['terminal'] and applied['source_count'] == len(applied['source_sha256'])
sources = applied['source_sha256']
assert len(sources) == 132
for name, value in sources.items():
    path = Path(name)
    assert not path.is_absolute() and '..' not in path.parts and (ROOT/path).resolve().is_relative_to(ROOT.resolve())
    assert re.fullmatch(r'[0-9a-f]{64}', value)
# R4 records actual command/exit/source guards, not R5 configuration metadata.
assert build['status'] == 'build_passed' and build['terminal'] and build['passed'] and build['exit_code'] == 0
assert not build['timed'] and build['source_unchanged'] and build['accepted_control_unchanged'] and build['fixed_baseline_unchanged']
assert build['application_sha256'] == args.applied_source_sha256
assert build['source_count'] == applied['source_count'] == 132 and build['source_sha256'] == sources
assert build['hashes_before'] == build['hashes_after']
assert len(build['command']) == 4 and build['command'][:3] == ['cmd.exe', '/d', '/c']
build_cmd = Path(build['command'][3]).resolve(strict=True)
assert build_cmd.is_relative_to((ROOT/'scratch/performance').resolve()) and build_cmd.suffix.lower() == '.cmd'
assert str(build_cmd) in build['hashes_before']
assert sha(build_cmd) == build['hashes_before'][str(build_cmd)] == proof['build_cmd_sha256']
cmd_lines = build_cmd.read_bytes().decode('utf-8-sig').replace('\r\n', '\n').splitlines()
assert r'cd /d D:\CantorAI\xlang3' in cmd_lines
assert proof['expected_cmd_build_line'] in cmd_lines
candidate_binaries = normalize_release(build['binaries_sha256'])
assert build['binary_file_count'] == len(candidate_binaries) == 178 and tree(RELEASE) == candidate_binaries
assert sha(RELEASE/'xlang3.exe') == build['exe_sha256'] == candidate_binaries['xlang3.exe']
assert sha(RELEASE/'xlang3_runtime.dll') == build['dll_sha256'] == candidate_binaries['xlang3_runtime.dll']
assert Path(build['log']).name == build['log']
build_log = (DATA/build['log']).resolve(strict=True)
assert build_log.is_relative_to(DATA.resolve()) and sha(build_log) == build['log_sha256']
assert focused['status'] == 'focused_passed' and focused['terminal'] and focused['passed'] and not focused['timed']
assert focused['hashes_unchanged'] and focused['hashes_before'] == focused['hashes_after']
assert focused['application_sha256'] == args.applied_source_sha256 and focused['build_sha256'] == args.build_receipt_sha256
assert focused['source_count'] == 132 and focused['source_sha256'] == sources
assert normalize_release(focused['binaries_sha256']) == candidate_binaries
assert len(focused['expected_names']) == len(set(focused['expected_names'])) == len(focused['phases']) == 19
assert [row['name'] for row in focused['phases']] == focused['expected_names']
assert all(row['passed'] and row['exit_code'] == 0 for row in focused['phases'])
control = read(CONTROL_MANIFEST, CONTROL_SHA)
validation = read(VALIDATION, VALIDATION_SHA)
assert control['status'] == 'preserved_fully_validated_lambda_capture_r5' and control['terminal']
assert control['full_validated'] and control['correctness_passed'] and control['fixed_gate_passed']
assert control['head'] == ACCEPTED_BASE and control['validation_sha256'] == VALIDATION_SHA
assert not control['remaining_official_sql_failures'] and (control['source_count'], control['file_count']) == (128, 178)
assert validation['status'] == 'trial_validated' and validation['terminal'] and validation['hashes_unchanged']
assert validation['full_validated'] and validation['correctness_passed'] and not validation['official_failures']
assert control['source_snapshot_sha256'] == validation['source_sha256']
assert set(control['source_snapshot_sha256']) <= set(sources)
control_release = CONTROL_MANIFEST.parent/'Release'
control_sources = CONTROL_MANIFEST.parent/'source-snapshot'
assert tree(control_release) == control['files_sha256'] and tree(control_sources) == control['source_snapshot_sha256']
assert control['files_sha256'] == normalize_release(validation['binaries_sha256'])
baseline = tree(BASELINE)
assert len(baseline) == 177 and baseline == control['fixed_baseline_sha256'] == validation['baseline_sha256']
gate_path = DATA/validation['fixed_gate']['output']
gate = read(gate_path, validation['fixed_gate']['sha256'])
assert validation['fixed_gate']['exit_code'] == 0 and gate['status'] == 'pass'
assert (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .10)
assert len(gate['cases']) == 11 and all(c['status'] == 'pass' for c in gate['cases'].values())
before = {}
def track(path, expected):
    path = str(Path(path).resolve(strict=True))
    assert sha(path) == expected and (path not in before or before[path] == expected)
    before[path] = expected
for path, expected in ((Path(__file__), proof['controller_sha256']), (PROOF, args.proof_sha256),
        (SOURCE, SOURCE_SHA), (ORIGINAL_CHILD, ORIGINAL_CHILD_SHA), (ORIGINAL_MANAGER, ORIGINAL_MANAGER_SHA),
        (ORIGINAL_PROOF, ORIGINAL_PROOF_SHA), (WATCH, WATCH_SHA), (applied_path, args.applied_source_sha256),
        (build_path, args.build_receipt_sha256), (build_log, build['log_sha256']),
        (focused_path, args.focused_receipt_sha256),
        (CONTROL_MANIFEST, CONTROL_SHA), (VALIDATION, VALIDATION_SHA), (gate_path, validation['fixed_gate']['sha256'])):
    track(path, expected)
for path, expected in CP_PINS.items(): track(path, expected)
for path, expected in original['original_sources'].items(): track(path, expected)
for path, expected in sources.items(): track(ROOT/path, expected)
for path, expected in applied['unowned_tracked_dirty_sha256'].items(): track(ROOT/path, expected)
for receipt in (build, focused):
    for path, expected in receipt['hashes_after'].items(): track(path, expected)
for row in focused['phases']:
    for stream in ('stdout', 'stderr'):
        path = Path(row[stream])
        assert path.name == row[stream]
        track(DATA/path, row[stream+'_sha256'])
for path, expected in candidate_binaries.items(): track(RELEASE/path, expected)
for path, expected in control['files_sha256'].items(): track(control_release/path, expected)
for path, expected in control['source_snapshot_sha256'].items(): track(control_sources/path, expected)
for path, expected in baseline.items(): track(BASELINE/path, expected)
head = subprocess.check_output(['git','rev-parse','HEAD'], cwd=ROOT, text=True).strip()
assert head == args.head
subprocess.run(['git','merge-base','--is-ancestor',ACCEPTED_BASE,head], cwd=ROOT, check=True)
assert not subprocess.check_output(['git','diff','--cached','--name-only'], cwd=ROOT)
orders = list(itertools.permutations(SELECTIONS))
case_orders = [tuple(order) for order in original['case_orders']]
assert len(orders) == 6 and len(case_orders) == 6
assert all(tuple(reversed(case_orders[i])) == case_orders[i+1] for i in (0,2,4))
record = dict(status='preflight', terminal=False, diagnostic_only=True, official_score=False,
    whole_goal_complete=False, head=head, accepted_base=ACCEPTED_BASE,
    applied_source=str(applied_path), applied_source_sha256=args.applied_source_sha256,
    build_receipt=str(build_path), build_receipt_sha256=args.build_receipt_sha256,
    focused_receipt=str(focused_path), focused_receipt_sha256=args.focused_receipt_sha256,
    focused_phase_count=19, candidate_build_cmd_sha256=sha(build_cmd),
    source_count=len(sources), source_sha256=sources, binaries_sha256=candidate_binaries,
    accepted_control_manifest_sha256=CONTROL_SHA, accepted_control_source_sha256=control['source_snapshot_sha256'],
    accepted_control_binaries_sha256=control['files_sha256'], baseline_sha256=baseline, hashes_before=before,
    phases=[], idle_guards=[], orders=orders, case_orders=case_orders, repeats_per_selection=6,
    operation_counts=dict(operations_per_case=10000, cases_per_child=4, children=18,
                          total_timed_case_loops=72, total_timed_operations=720000),
    candidate_correctness_scope='Actual R4 build and same-source19 untimed focused phases passed and pinned; no new full correctness/gate acceptance is asserted by this artificial diagnostic.',
    scope='All six CP/control/candidate permutations; each runtime occupies every position twice. Original four artificial loops and mirrored case orders retained; no native date timing, official score, workload share, trimming, retry or predicted gain')
def save():
    OUT.write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')
def stable():
    try:
        return (all(sha(p) == h for p,h in before.items()) and tree(RELEASE) == candidate_binaries
                and tree(control_release) == control['files_sha256']
                and tree(control_sources) == control['source_snapshot_sha256'] and tree(BASELINE) == baseline
                and subprocess.check_output(['git','rev-parse','HEAD'], cwd=ROOT, text=True).strip() == head)
    except BaseException:
        return False
def idle(name):
    command = 'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'
    raw = subprocess.check_output(['powershell','-NoProfile','-Command',command])
    rows = json.loads(raw.decode('utf-8-sig') or '[]')
    if isinstance(rows,dict): rows = [rows]
    tools = {'cl.exe','link.exe','ninja.exe','msbuild.exe','cmake.exe','ctest.exe','nmake.exe','lld-link.exe','clang-cl.exe','sample-pprint-native-cpu-20261008.exe'}
    busy = [r for r in rows if r['ProcessId'] != os.getpid() and (r['Name'].lower() in tools or r['Name'].lower().startswith(('python','xlang3')))]
    record['idle_guards'].append(dict(phase=name,busy=busy))
    save()
    return busy
spec = importlib.util.spec_from_file_location('constructor_entry_three_runtime_watch',WATCH)
watcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(watcher)
env_base = os.environ.copy()
env_base['XLANG3_PYTHON_LIB'] = str(CP.parent/'Lib')
for key in ['PYTHONPATH','PYTHONHOME','PYTHONOPTIMIZE','PYTHONIOENCODING','PYTHONPYCACHEPREFIX','XLANG3_VM_OPCODE_TIMING']:
    env_base.pop(key,None)
executables = {'cpython-python':CP,'xlang3-control':control_release/'xlang3.exe','xlang3-candidate':RELEASE/'xlang3.exe'}
try:
    save()
    assert not idle('preflight') and stable()
    for round_index,order in enumerate(orders):
        for selection in order:
            name = 'round-'+str(round_index+1)+'-'+selection
            assert not idle('before-'+name) and stable()
            case_order = case_orders[round_index]
            executable = executables[selection]
            dll = executable.with_name('python314.dll' if selection == 'cpython-python' else 'xlang3_runtime.dll')
            command = [str(executable)] + (['-I'] if selection == 'cpython-python' else []) + [str(SOURCE),'--selection',selection,
                '--case-order',','.join(case_order),'--runtime-executable',str(executable),'--exe-sha256',sha(executable),'--dll-sha256',sha(dll)]
            env = dict(env_base,PATH=str(executable.parent)+os.pathsep+env_base.get('PATH',''))
            stdout,stderr = DATA/(PREFIX+'-'+name+'.stdout.log'),DATA/(PREFIX+'-'+name+'.stderr.log')
            row = dict(name=name,round_index=round_index,selection=selection,command=command,
                       passed=False,timing_accepted=False,timeout=False,case_order=case_order)
            record['phases'].append(row)
            save()
            finish = watcher.start_timing_process_watch(PREFIX,name,row)
            child = None
            try:
                with stdout.open('xb') as out,stderr.open('xb') as err:
                    child = subprocess.Popen(command,cwd=ROOT,env=env,stdin=subprocess.DEVNULL,stdout=out,stderr=err,
                                             creationflags=subprocess.CREATE_NO_WINDOW)
                    row['pid'] = child.pid
                    save()
                    try:
                        row['exit_code'] = child.wait(timeout=120)
                    except subprocess.TimeoutExpired:
                        row['timeout'] = True
                assert row.get('exit_code') == 0 and not row['timeout'] and not stderr.read_bytes()
                lines = stdout.read_bytes().splitlines()
                assert len(lines) == 1
                result = json.loads(lines[0])
                assert result['status'] == 'artificial_constructor_entry_diagnostic_passed' and result['hashes_unchanged'] and result['selection'] == selection
                assert result['case_order'] == list(case_order) and set(result['seconds']) == set(case_order)
                assert result['original_callable_identities_unchanged'] and result['plain_slots_empty'] and result['inherited_object_init_unchanged']
                assert result['selected_date_is_python_date'] and result['selected_date_is_public_date'] == (selection != 'cpython-python')
                assert result['date_new']['has_python_code'] and result['plain_new']['has_python_code']
                assert result['operations_per_case'] == 10000 and result['timed_operation_count'] == 40000
                assert result['untimed_validation_warm_calls_per_case'] == 1
                assert result['expected_state_hex'] == '07bc0507' and result['expected_fields'] == [1980,5,7]
                assert not result['profile_enabled'] and not result['trace_enabled'] and not result['original_pickle_body_run'] and not result['workload_cost_fraction_estimated']
                assert all(math.isfinite(value) and value > 0 for value in result['seconds'].values())
                row.update(passed=True,result=result)
            except BaseException as error:
                row['error'] = repr(error)
            finally:
                try:
                    if child is not None and child.poll() is None:
                        cleanup = subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)],capture_output=True,timeout=15)
                        row['cleanup_exit_code'] = cleanup.returncode
                        if child.poll() is None: child.kill()
                        child.wait(timeout=15)
                    row['owned_child_cleanup_completed'] = True
                except BaseException as error:
                    row.update(owned_child_cleanup_completed=False,cleanup_error=repr(error),passed=False)
                row['stdout_sha256'],row['stderr_sha256'] = maybe_sha(stdout),maybe_sha(stderr)
                row.update(stdout_log=stdout.name,stderr_log=stderr.name)
                try:
                    row['measurement_valid'] = finish()
                except BaseException as error:
                    row.update(measurement_valid=False,watcher_finish_error=repr(error),passed=False)
                try:
                    row['post_idle_guard_passed'] = not idle('after-'+name)
                except BaseException as error:
                    row.update(post_idle_guard_passed=False,post_idle_guard_error=repr(error),passed=False)
                row['post_hashes_stable'] = stable()
                row['timing_accepted'] = (row['passed'] and row['measurement_valid'] and row['owned_child_cleanup_completed']
                    and row['post_idle_guard_passed'] and row['post_hashes_stable']
                    and row['stdout_sha256'] is not None and row['stderr_sha256'] is not None)
                save()
            assert row['timing_accepted'],name
            print(name,'PASS',flush=True)
    summary = {}
    for selection in SELECTIONS:
        rows = [r['result'] for r in record['phases'] if r['selection'] == selection]
        assert len(rows) == 6
        summary[selection] = {case:dict(seconds=[r['seconds'][case] for r in rows],
            median_microseconds_per_call=statistics.median(r['seconds'][case] for r in rows)*1e6/10000) for case in case_orders[0]}
    record.update(status='terminal_artificial_constructor_entry_diagnostic_passed',summary=summary)
except BaseException as error:
    record.update(status='terminal_artificial_constructor_entry_diagnostic_failed',error=repr(error))
finally:
    record.update(terminal=True,hashes_after={p:maybe_sha(p) for p in before},hashes_unchanged=stable(),controller_sha256=sha(Path(__file__)))
    if not record['hashes_unchanged']: record['status'] = 'terminal_invalid_hash_drift'
    save()
print(record['status'],sha(OUT))
print(json.dumps(record.get('summary',{})))
raise SystemExit(0 if record['status'] == 'terminal_artificial_constructor_entry_diagnostic_passed' and record['hashes_unchanged'] else 1)
