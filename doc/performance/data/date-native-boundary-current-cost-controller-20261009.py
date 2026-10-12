"""Strictly watched, balanced artificial date-operation diagnostic; no suite score."""
import ast
import hashlib
import importlib.util
import itertools
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
SOURCE = ROOT / 'scratch/performance/date-native-boundary-loop-cost-proposed-20261009.py'
PROOF = ROOT / 'scratch/performance/date-native-boundary-loop-cost-provenance-proposed-20261009.json'
WATCH = ROOT / 'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'
PREFIX = 'date-native-boundary-loop-cost-accepted-r5-r2-20261009'
OUT = DATA / (PREFIX + '.json')
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7)
assert sys.flags.isolated and not sys.flags.optimize and not any(DATA.glob(PREFIX + '*'))
assert sha(SOURCE) == '348590de539ceb46d3fb557c00169da7ad252fefea9bbbe96b1ed05df738039f'
assert sha(PROOF) == '3c3f66573f71077c942d11e9b97b1c3aa0e5a5c87be91adceaa74f520648a8ef'
assert sha(WATCH) == '50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf'
ast.parse(SOURCE.read_bytes())
vpath = DATA / 'lambda-eager-comprehension-capture-r5-validation-r5-20261009.json'
assert sha(vpath) == '6a41ee3fe92453d6472f2bb1c24b2da7ef125ac8425d27efdffa9f2262ab2d35'
v = json.loads(vpath.read_bytes())
assert v['terminal'] and v['full_validated'] and v['hashes_unchanged']
assert v['correctness_passed'] and not v['official_failures']
gate_path = DATA / v['fixed_gate']['output']
assert sha(gate_path) == v['fixed_gate']['sha256']
assert json.loads(gate_path.read_bytes())['status'] == 'pass' and v['fixed_gate']['exit_code'] == 0
proof = json.loads(PROOF.read_bytes())
before = {str(ROOT / p): h for p, h in (v['source_sha256'] | v['binaries_sha256']).items()}
for p in [Path(__file__), SOURCE, PROOF, WATCH, vpath, gate_path, CP, CP.parent / 'python314.dll']:
    before[str(p)] = sha(p)
before.update({str(Path(p)): h for p, h in proof['original_sources'].items()})
untimed_path = ROOT / proof['prior_untimed_receipt']
assert sha(untimed_path) == proof['prior_untimed_receipt_sha256']
before[str(untimed_path)] = sha(untimed_path)
untimed = json.loads(untimed_path.read_bytes())
native_file = untimed['phases'][0]['result']['native_datetime_file']
if native_file:
    before[native_file] = sha(native_file)
release = tree(RELEASE)
baseline = tree(ROOT / 'build-repro/Release')
assert baseline == v['baseline_sha256'] and len(release) == 178
assert all(sha(p) == h for p, h in before.items())
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
assert head == 'b188b24e86eac9cbc10c7f0efe6234d98caa0009'
assert not subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT)
orders = list(itertools.permutations(['cpython-native', 'cpython-python', 'xlang3-public']))
record = dict(status='preflight', terminal=False, diagnostic_only=True, official_score=False,
              whole_goal_complete=False, head=head, source_count=128, source_sha256=v['source_sha256'],
              binaries_sha256=v['binaries_sha256'], baseline_sha256=baseline, hashes_before=before,
              phases=[], idle_guards=[], orders=orders, repeats_per_selection=6,
              operation_counts=dict(reductions=10000, restorations=10000),
              scope='Artificial component loops with all six selection permutations; loop/result replacement included, no original pickle body, workload cost share or inferred suite gain')
def save():
    OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
def stable():
    return all(sha(p) == h for p, h in before.items()) and tree(RELEASE) == release and tree(ROOT / 'build-repro/Release') == baseline
def idle(name):
    command = 'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'
    raw = subprocess.check_output(['powershell', '-NoProfile', '-Command', command])
    rows = json.loads(raw.decode('utf-8-sig') or '[]')
    if isinstance(rows, dict):
        rows = [rows]
    tools = {'cl.exe', 'link.exe', 'ninja.exe', 'msbuild.exe', 'cmake.exe', 'ctest.exe', 'nmake.exe', 'lld-link.exe', 'clang-cl.exe', 'sample-pprint-native-cpu-20261008.exe'}
    busy = [r for r in rows if r['ProcessId'] != os.getpid() and (r['Name'].lower() in tools or r['Name'].lower().startswith(('python', 'xlang3')))]
    record['idle_guards'].append(dict(phase=name, busy=busy))
    save()
    return busy
spec = importlib.util.spec_from_file_location('date_component_watch', WATCH)
watcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(watcher)
env = os.environ.copy()
env['PATH'] = str(RELEASE) + os.pathsep + env.get('PATH', '')
env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
for key in ['PYTHONPATH', 'PYTHONHOME', 'PYTHONOPTIMIZE', 'PYTHONIOENCODING', 'PYTHONPYCACHEPREFIX', 'XLANG3_VM_OPCODE_TIMING']:
    env.pop(key, None)
try:
    save()
    assert not idle('preflight') and stable()
    for round_index, order in enumerate(orders):
        for selection in order:
            name = 'round-' + str(round_index + 1) + '-' + selection
            assert not idle('before-' + name) and stable()
            executable = RELEASE / 'xlang3.exe' if selection == 'xlang3-public' else CP
            dll = RELEASE / 'xlang3_runtime.dll' if selection == 'xlang3-public' else CP.parent / 'python314.dll'
            command = [str(executable)] + ([] if selection == 'xlang3-public' else ['-I']) + [str(SOURCE), '--selection', selection, '--exe-sha256', sha(executable), '--dll-sha256', sha(dll)]
            stdout, stderr = DATA / (PREFIX + '-' + name + '.stdout.log'), DATA / (PREFIX + '-' + name + '.stderr.log')
            row = dict(name=name, round_index=round_index, selection=selection, command=command,
                       passed=False, timing_accepted=False, timeout=False)
            record['phases'].append(row)
            save()
            finish = watcher.start_timing_process_watch(PREFIX, name, row)
            child = None
            try:
                with stdout.open('xb') as out, stderr.open('xb') as err:
                    child = subprocess.Popen(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL, stdout=out, stderr=err,
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
                assert result['status'] == 'artificial_date_loop_diagnostic_passed' and result['hashes_unchanged'] and result['selection'] == selection
                assert result['timed_reduction_calls'] == result['timed_state_restoration_calls'] == 10000
                assert result['expected_state_hex'] == '07bc0507' and result['expected_fields'] == [1980, 5, 7]
                assert not result['profile_enabled'] and not result['trace_enabled'] and not result['original_pickle_body_run'] and not result['workload_cost_fraction_estimated']
                assert all(math.isfinite(result[key]) and result[key] > 0 for key in ['reduction_seconds', 'restoration_seconds'])
                row.update(passed=True, result=result)
            except BaseException as error:
                row['error'] = repr(error)
            finally:
                try:
                    if child is not None and child.poll() is None:
                        cleanup = subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=15)
                        row['cleanup_exit_code'] = cleanup.returncode
                        if child.poll() is None:
                            child.kill()
                        child.wait(timeout=15)
                    row['owned_child_cleanup_completed'] = True
                except BaseException as error:
                    row.update(owned_child_cleanup_completed=False, cleanup_error=repr(error), passed=False)
                row['stdout_sha256'] = sha(stdout) if stdout.exists() else None
                row['stderr_sha256'] = sha(stderr) if stderr.exists() else None
                row.update(stdout_log=stdout.name, stderr_log=stderr.name)
                row['measurement_valid'] = finish()
                row['post_idle_guard_passed'] = not idle('after-' + name)
                row['post_hashes_stable'] = stable()
                row['timing_accepted'] = row['passed'] and row['measurement_valid'] and row['owned_child_cleanup_completed'] and row['post_idle_guard_passed'] and row['post_hashes_stable']
                save()
            assert row['timing_accepted'], name
            print(name, 'PASS', flush=True)
    summary = {}
    for selection in ['cpython-native', 'cpython-python', 'xlang3-public']:
        rows = [r['result'] for r in record['phases'] if r['selection'] == selection]
        assert len(rows) == 6
        summary[selection] = {op: dict(seconds=[r[op + '_seconds'] for r in rows], median_microseconds_per_call=statistics.median(r[op + '_seconds'] for r in rows) * 1e6 / 10000)
                              for op in ['reduction', 'restoration']}
    record.update(status='terminal_artificial_date_diagnostic_passed', summary=summary)
except BaseException as error:
    record.update(status='terminal_artificial_date_diagnostic_failed', error=repr(error))
finally:
    record.update(terminal=True, hashes_after={p: sha(p) for p in before}, hashes_unchanged=stable(), controller_sha256=sha(Path(__file__)))
    if not record['hashes_unchanged']:
        record['status'] = 'terminal_invalid_hash_drift'
    save()
print(record['status'], sha(OUT))
print(json.dumps(record.get('summary', {})))
raise SystemExit(0 if record['status'] == 'terminal_artificial_date_diagnostic_passed' and record['hashes_unchanged'] else 1)
