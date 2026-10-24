"""Fixed default regression gate and unchanged original Coverage attempt."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BASELINE = ROOT / 'build-repro/Release'
CP = Path('C:/Python/Python314/python.exe')
PREFIX = 'trace-disable-local-events-performance-20261009'
CORRECTNESS = DATA / 'trace-disable-local-events-correctness-20261009.json'
WATCH = ROOT / 'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'
SITE = ROOT / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CP.resolve()
    assert sha(CORRECTNESS) == '617a0841153d9f7e9620c7b23fb5c00ab55ad4e5c81b930a792e7ec773dc3e98'
    correct = json.loads(CORRECTNESS.read_text())
    assert correct['terminal'] and correct['status'] == 'correctness_passed_performance_pending'
    assert correct['sources_unchanged'] and correct['release_unchanged'] and correct['fixed_baseline_unchanged']
    assert sha(WATCH) == '50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf'
    spec = importlib.util.spec_from_file_location('trace_gate_watch', WATCH)
    watcher = importlib.util.module_from_spec(spec); spec.loader.exec_module(watcher)
    application = json.loads((DATA / 'trace-disable-local-events-applied-source-20261009.json').read_text())
    pins = {str(ROOT / p): h for p, h in correct['source_sha256'].items()}
    pins.update({str(RELEASE / p): h for p, h in correct['release_sha256'].items()})
    pins.update({str(BASELINE / p): h for p, h in application['fixed_baseline_sha256'].items()})
    pins.update({str(ROOT / p): h for p, h in application['unowned_tracked_dirty_sha256'].items()})
    gate_script = ROOT / 'benchmarks/check_regression.py'
    gate_cases = runpy.run_path(str(gate_script))['CASES']
    gate_sources = {name: sha(ROOT / 'benchmarks/cases' / (name + '.py')) for name in gate_cases}
    pins[str(gate_script)] = sha(gate_script)
    assert len(gate_sources) == 11
    for name, expected in gate_sources.items():
        pins[str(ROOT / 'benchmarks/cases' / (name + '.py'))] = expected
    runner = ROOT / 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py'
    pins[str(runner)] = sha(runner)
    receipt = DATA / (PREFIX + '.json')
    assert not receipt.exists()
    record = {'status': 'running', 'terminal': False, 'phases': [], 'idle_guards': [],
        'correctness_sha256': sha(CORRECTNESS), 'source_count': correct['source_count'],
        'tracked_sha256_before': pins, 'controller_sha256': sha(__file__),
        'scope': 'Unchanged fixed default11 gate and original Coverage; suite completion remains independent of the trace persistence repair.'}
    def save():
        receipt.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
    def stable():
        return all(Path(p).is_file() and sha(p) == h for p, h in pins.items())
    def idle(label):
        command = 'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'
        result = subprocess.run(['powershell', '-NoProfile', '-Command', command], capture_output=True, check=True)
        processes = json.loads(result.stdout.decode('utf-8-sig') or '[]')
        if isinstance(processes, dict): processes = [processes]
        names = {'cl.exe', 'link.exe', 'ninja.exe', 'msbuild.exe', 'cmake.exe', 'ctest.exe', 'nmake.exe', 'clang-cl.exe', 'lld-link.exe'}
        busy = [p for p in processes if p['ProcessId'] != os.getpid() and
                (p['Name'].lower() in names or p['Name'].lower().startswith(('python', 'xlang3')))]
        record['idle_guards'].append({'phase': label, 'busy': busy})
        save()
        assert not busy, busy
    env = os.environ.copy()
    for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE', 'PYTHONHOME'):
        env.pop(name, None)
    env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
    def phase(name, command, timeout):
        idle('before-' + name); assert stable()
        row = {'name': name, 'command': [str(v) for v in command]}
        record['phases'].append(row); save()
        finish = watcher.start_timing_process_watch(PREFIX, name, row)
        child = None
        stdout = DATA / (PREFIX + '-' + name + '.stdout.log')
        stderr = DATA / (PREFIX + '-' + name + '.stderr.log')
        try:
            with stdout.open('xb') as out, stderr.open('xb') as err:
                child = subprocess.Popen(row['command'], cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                    stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
                row['pid'] = child.pid; save()
                row['exit_code'] = child.wait(timeout=timeout)
        finally:
            if child is not None and child.poll() is None:
                subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=10)
                child.wait(timeout=10)
            row['owned_child_cleanup_completed'] = child is not None and child.poll() is not None
            row['measurement_valid'] = finish()
            row.update(stdout=stdout.name, stdout_sha256=sha(stdout), stderr=stderr.name, stderr_sha256=sha(stderr))
            save()
        idle('after-' + name)
        assert stable() and row['measurement_valid'] and row['owned_child_cleanup_completed']
        print(name + ': exit ' + str(row['exit_code']), flush=True)
        return row
    try:
        save(); assert stable()
        gate_output = DATA / (PREFIX + '-fixed-gate.json')
        gate_row = phase('fixed-gate', [CP, gate_script, '--baseline', BASELINE / 'xlang3.exe',
            '--candidate', RELEASE / 'xlang3.exe', '--output', gate_output], 900)
        gate = json.loads(gate_output.read_text())
        assert gate_row['exit_code'] == 0 and gate['status'] == 'pass'
        assert (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, 0.1)
        assert set(gate['cases']) == set(gate_sources)
        assert all(v['source_sha256'] == gate_sources[k] for k, v in gate['cases'].items())
        record['fixed_gate'] = {'exit_code': 0, 'output': gate_output.name, 'sha256': sha(gate_output), 'passed': True}
        save()
        env.update(PYTHONPATH=str(ROOT / 'benchmarks/diagnostics/pyperf_compat'), PYTHONIOENCODING='utf-8')
        official = DATA / (PREFIX + '-coverage-fast.json')
        coverage = phase('official-coverage', [CP, runner, '--runtime', RELEASE / 'xlang3.exe',
            '--benchmarks', 'coverage', '--mode', 'fast', '--case-timeout', '300',
            '--dependency-site', SITE, '--output', official], 360)
        text = (DATA / coverage['stdout']).read_text(encoding='utf-8', errors='replace') + (DATA / coverage['stderr']).read_text(encoding='utf-8', errors='replace')
        record['coverage'] = {'exit_code': coverage['exit_code'], 'completed': coverage['exit_code'] == 0,
            'trace_changed_warning_present': 'trace-changed' in text,
            'print_exception_type_error_present': 'print_exception(): Exception expected for value, object found' in text,
            'output_sha256': sha(official) if official.is_file() else None,
            'scope': 'Original benchmark attempt. A remaining formatter error is not a speed score.'}
        record['status'] = 'fixed_gate_passed_coverage_completed' if coverage['exit_code'] == 0 else 'fixed_gate_passed_coverage_failed'
    except BaseException as error:
        record.update(status='validation_failed_or_invalid', error=repr(error))
    finally:
        record.update(terminal=True, hashes_unchanged=stable(),
            tracked_sha256_after={p: sha(p) if Path(p).is_file() else None for p in pins})
        if not record['hashes_unchanged']:
            record['status'] = 'invalid_hash_drift'
        save()
    print(json.dumps({'status': record['status'], 'fixed_gate': record.get('fixed_gate'),
                      'coverage': record.get('coverage'), 'receipt_sha256': sha(receipt)}, indent=2), flush=True)
    return 0 if record['status'].startswith('fixed_gate_passed_') else 2

if __name__ == '__main__':
    raise SystemExit(main())
