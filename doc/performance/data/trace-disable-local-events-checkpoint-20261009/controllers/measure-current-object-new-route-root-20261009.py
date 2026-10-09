"""Balanced artificial route differential; no official score or projected gain."""
import hashlib
import importlib.util
import itertools
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
CP = Path('C:/Python/Python314/python.exe')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BASELINE = ROOT / 'build-repro/Release'
CONTROL_ROOT = ROOT / 'build-repro/controls/lambda-eager-comprehension-capture-accepted-r5-20261009'
CONTROL_MANIFEST = CONTROL_ROOT / 'preserved-release-provenance.json'
CORRECTNESS = DATA / 'trace-disable-local-events-correctness-20261009.json'
VERIFIED = DATA / 'object-new-route-untimed-ir-verification-20261009.json'
CAPTURE = DATA / 'object-new-route-untimed-20261009.json'
CHILD = ROOT / 'scratch/performance/object-new-static-lookup-diagnostic-child-20261009.py'
WATCH = ROOT / 'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'
PREFIX = 'object-new-route-current-balanced-20261009'
OUT = DATA / (PREFIX + '.json')
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda folder: {p.relative_to(folder).as_posix(): sha(p) for p in folder.rglob('*') if p.is_file()}
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated
for path, expected in ((CONTROL_MANIFEST, '9271314856ede7f173396b2f39449a01d785f61a1579ce5f93c7786a2961dfad'),
    (CORRECTNESS, '617a0841153d9f7e9620c7b23fb5c00ab55ad4e5c81b930a792e7ec773dc3e98'),
    (VERIFIED, '8114c28bc2a6057d290e009d0b906c0e30816ba01466851216d6512e0b3038c8'),
    (CAPTURE, 'bd6fd41c0f8f05952d529f6ac85c7a036dbe5684090815edef511edca13618c7'),
    (CHILD, 'b1f32d611604d5ef67575bf1c6cdb4c6cd6f1bf7281aef37c13c228ef6ef2ee2'),
    (WATCH, '50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf')):
    assert sha(path) == expected, str(path)
control, correct, verified, capture = (json.loads(p.read_bytes()) for p in (CONTROL_MANIFEST, CORRECTNESS, VERIFIED, CAPTURE))
assert control['full_validated'] and control['fixed_gate_passed'] and control['correctness_passed']
assert (control['source_count'], control['file_count']) == (128, 178)
assert correct['status'] == 'correctness_passed_performance_pending' and correct['source_count'] == 137
assert verified['status'] == 'untimed_semantics_and_ir_verified' and all(verified['checks'].values())
assert tree(RELEASE) == correct['release_sha256']
assert tree(CONTROL_ROOT / 'Release') == control['files_sha256']
assert tree(CONTROL_ROOT / 'source-snapshot') == control['source_snapshot_sha256']
assert tree(BASELINE) == control['fixed_baseline_sha256'] and len(tree(BASELINE)) == 177
assert not any(DATA.glob(PREFIX + '*'))
current_verification_path = DATA / 'object-new-route-current-verification-20261009.json'
assert sha(current_verification_path) == '2eed66816104ad7be26da62413b306fbf080a2271e848a471e8d99cea9a86981'
current_verification = json.loads(current_verification_path.read_bytes())
assert current_verification['status'] == 'current_route_verification_passed' and current_verification['hashes_unchanged']
assert current_verification['correctness_sha256'] == sha(CORRECTNESS)
pins = dict(current_verification['pins_before'])
pins[str(current_verification_path)] = sha(current_verification_path)
for folder, mapping in ((CONTROL_ROOT / 'Release', control['files_sha256']),
    (CONTROL_ROOT / 'source-snapshot', control['source_snapshot_sha256']), (BASELINE, control['fixed_baseline_sha256'])):
    pins.update({str(folder / p): h for p, h in mapping.items()})
for path in (CONTROL_MANIFEST, CORRECTNESS, VERIFIED, CAPTURE, CHILD, WATCH, Path(__file__)):
    pins[str(path)] = sha(path)
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
assert head == '09b0500e44820a4e557ca2774459957534702ddd'
selections = ('cpython-python', 'xlang3-control', 'xlang3-current')
orders = list(itertools.permutations(selections))
cases = ('original_lookup', 'saved_native_lookup')
executables = {'cpython-python': CP, 'xlang3-control': CONTROL_ROOT / 'Release/xlang3.exe', 'xlang3-current': RELEASE / 'xlang3.exe'}
record = {'terminal': False, 'status': 'preflight', 'timed': True, 'diagnostic_only': True,
    'head': head, 'candidate': 'current combined trace-repair source137 build; not the earlier R4 capture',
    'control': 'accepted lambda capture R5', 'orders': orders, 'phases': [], 'idle_guards': [],
    'pins_before': pins, 'expected_timed_operations': 360000,
    'scope': 'Six runtime permutations, mirrored two-case orders, 18 serial children and 36 unchanged 10000-allocation loops. Difference includes lookup, dispatch and different global/register loads. No pure lookup CPU share, official score or projected suite gain.',
    'loaded_dll_limit': 'Executable directory is first on PATH and EXE/DLL file bytes are pinned. No claim that file hashing independently enumerates every loaded image.'}
def save():
    OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
def stable():
    return all(Path(p).is_file() and sha(p) == h for p, h in pins.items()) and subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == head
def idle(label):
    raw = subprocess.check_output(['powershell', '-NoProfile', '-Command', 'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'])
    rows = json.loads(raw.decode('utf-8-sig') or '[]')
    if isinstance(rows, dict): rows = [rows]
    names = {'cl.exe', 'link.exe', 'ninja.exe', 'msbuild.exe', 'cmake.exe', 'ctest.exe', 'nmake.exe', 'clang-cl.exe', 'lld-link.exe'}
    busy = [p for p in rows if p['ProcessId'] != os.getpid() and (p['Name'].lower() in names or p['Name'].lower().startswith(('python', 'xlang3')))]
    record['idle_guards'].append({'label': label, 'busy': busy})
    save()
    assert not busy, busy
spec = importlib.util.spec_from_file_location('route_watch', WATCH)
watcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(watcher)
env = os.environ.copy()
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE', 'PYTHONHOME', 'XLANG3_VM_OPCODE_TIMING'):
    env.pop(name, None)
env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
try:
    idle('preflight')
    assert stable()
    for round_number, order in enumerate(orders, 1):
        case_order = cases if round_number % 2 else tuple(reversed(cases))
        for selection in order:
            label = 'round-' + str(round_number) + '-' + selection
            idle('before-' + label)
            assert stable()
            exe = executables[selection]
            dll = exe.with_name('python314.dll' if selection == 'cpython-python' else 'xlang3_runtime.dll')
            command = [str(exe), *(['-I'] if selection == 'cpython-python' else []), str(CHILD),
                '--selection', selection, '--case-order', ','.join(case_order), '--runtime-executable', str(exe),
                '--exe-sha256', sha(exe), '--dll-sha256', sha(dll), '--source-sha256', sha(CHILD)]
            row = {'label': label, 'selection': selection, 'case_order': case_order, 'command': command}
            record['phases'].append(row)
            finish = watcher.start_timing_process_watch(PREFIX, label, row)
            child = None
            stdout, stderr = DATA / (PREFIX + '-' + label + '.stdout.log'), DATA / (PREFIX + '-' + label + '.stderr.log')
            try:
                local = dict(env, PATH=str(exe.parent) + os.pathsep + env.get('PATH', ''))
                with stdout.open('xb') as out, stderr.open('xb') as err:
                    child = subprocess.Popen(command, cwd=ROOT, env=local, stdin=subprocess.DEVNULL,
                        stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
                    row['pid'] = child.pid
                    save()
                    row['exit_code'] = child.wait(timeout=120)
            finally:
                if child is not None and child.poll() is None:
                    subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=10)
                    child.wait(timeout=10)
                row['measurement_valid'] = finish()
                row['stdout_sha256'], row['stderr_sha256'] = sha(stdout), sha(stderr)
                save()
            idle('after-' + label)
            assert row['exit_code'] == 0 and row['measurement_valid'] and stable(), row
            result = json.loads(stdout.read_text(encoding='utf-8'))
            assert not result['verify_only'] and result['timed_operation_count'] == 20000 and result['hashes_unchanged']
            assert set(result['seconds']) == set(cases) and all(v > 0 for v in result['seconds'].values())
            row['result'] = result
            save()
            print(label + ': passed', flush=True)
    record['medians_us'] = {selection: {case: statistics.median(p['result']['seconds'][case] * 1e6 / 10000
        for p in record['phases'] if p['selection'] == selection) for case in cases} for selection in selections}
    record['within_child_original_over_saved'] = {selection: [p['result']['seconds']['original_lookup'] / p['result']['seconds']['saved_native_lookup']
        for p in record['phases'] if p['selection'] == selection] for selection in selections}
    record['status'] = 'diagnostic_passed'
except BaseException as error:
    record.update(status='diagnostic_failed_or_invalid', error=repr(error))
finally:
    record['terminal'] = True
    record['hashes_unchanged'] = stable()
    if not record['hashes_unchanged']: record['status'] = 'invalid_hash_drift'
    save()
print(json.dumps({'status': record['status'], 'medians_us': record.get('medians_us'), 'receipt_sha256': sha(OUT)}, indent=2), flush=True)
raise SystemExit(0 if record['status'] == 'diagnostic_passed' else 2)
