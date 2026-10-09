"""Complete unchanged fixed gate and original GC definitions in CP3.14.7 and X."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import runpy
import statistics
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BASELINE = ROOT / 'build-repro/Release'
APP = DATA / 'gc-generic-cycles-applied-source-r4-20261009.json'
CORRECT = DATA / 'gc-generic-cycles-correctness-r4-20261009.json'
WATCH = ROOT / 'scratch/performance/gc-dormant-msbuild-activity-watch-20261009.py'
RUNNER = ROOT / 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py'
SITE = ROOT / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'
PREFIX = 'gc-generic-cycles-performance-r4-activity-20261009'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(p.read_bytes())

def main():
    assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7)
    assert sha(APP) == '8b00bc5a7bb84a4440a32c4e789229e6000434829b70c0c0e08adb3496f45362'
    assert sha(CORRECT) == '6edf927bae4e535a9f23f309c5e3172f89f6e8167023cd55c29b120d235e660c'
    assert sha(WATCH) == '016539435a7182d77232cd9129bba474deb42f0fecfd718c505779b721b28f86'
    app, correct = read(APP), read(CORRECT)
    assert correct['terminal'] and correct['correctness_passed'] and correct['sources_unchanged']
    assert correct['release_unchanged'] and correct['fixed_baseline_unchanged']
    pins = {str(ROOT / p): h for p, h in correct['source_sha256'].items()}
    pins.update({str(RELEASE / p): h for p, h in correct['release_sha256'].items()})
    pins.update({str(BASELINE / p): h for p, h in app['fixed_baseline_sha256'].items()})
    pins.update({str(ROOT / p): h for p, h in app['unowned_tracked_dirty_sha256'].items()})
    gate_script = ROOT / 'benchmarks/check_regression.py'
    cases = runpy.run_path(str(gate_script))['CASES']
    gate_sources = {name: sha(ROOT / 'benchmarks/cases' / (name + '.py')) for name in cases}
    assert len(gate_sources) == 11
    pins.update({str(ROOT / 'benchmarks/cases' / (name + '.py')): h for name, h in gate_sources.items()})
    for p in (CP, CP.with_name('python314.dll'), APP, CORRECT, WATCH, RUNNER, gate_script, Path(__file__), ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'):
        pins[str(p)] = sha(p)
    for definition in ('bm_gc_collect', 'bm_gc_traversal'):
        p = CP.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks' / definition / 'run_benchmark.py'
        pins[str(p)] = sha(p)
    out = DATA / (PREFIX + '.json')
    assert not out.exists()
    record = {'terminal': False, 'status': 'running', 'phases': [], 'idle_guards': [], 'pins_before': pins,
        'correctness_sha256': sha(CORRECT), 'source_count': app['source_count'], 'engine_commit_permitted': False,
        'scope': 'Default11/21repeats/5warmups/.10 fixed gate, then unchanged original gc_collect and gc_traversal in fresh CPython3.14.7 and candidate. Gate failure is retained and does not permit an engine commit; affected results alone do not update full97.'}
    def save():
        out.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
    def stable():
        return all(Path(p).is_file() and sha(p) == h for p, h in pins.items())
    def idle(label):
        observation = watcher.idle_snapshot()
        record['idle_guards'].append({'phase': label, **observation}); save()
        assert not observation['busy'], observation['busy']
    spec = importlib.util.spec_from_file_location('generic_gc_watch', WATCH)
    watcher = importlib.util.module_from_spec(spec); spec.loader.exec_module(watcher)
    record['dormant_worker_admission'] = watcher.admit_dormant_worker()
    env = os.environ.copy()
    for key in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE', 'PYTHONHOME', 'XLANG3_VM_OPCODE_TIMING'):
        env.pop(key, None)
    env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
    def phase(name, command, timeout):
        idle('before-' + name); assert stable()
        row = {'name': name, 'command': [str(v) for v in command]}
        record['phases'].append(row); save()
        stdout, stderr = (DATA / (PREFIX + '-' + name + suffix) for suffix in ('.stdout.log', '.stderr.log'))
        finish = watcher.start_timing_process_watch(PREFIX, name, row)
        child = None
        try:
            with stdout.open('xb') as stream, stderr.open('xb') as err:
                child = subprocess.Popen(row['command'], cwd=ROOT, env=env, stdout=stream, stderr=err,
                    stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
                row['pid'] = child.pid; save()
                row['exit_code'] = child.wait(timeout=timeout)
        finally:
            if child is not None and child.poll() is None:
                subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=10)
                child.wait(timeout=10)
            row['owned_child_cleanup_completed'] = child is not None and child.poll() is not None
            row['measurement_valid'] = finish()
            row.update(stdout=stdout.name, stdout_sha256=sha(stdout), stderr=stderr.name, stderr_sha256=sha(stderr)); save()
        idle('after-' + name)
        assert stable() and row['measurement_valid']
        print(name + ': exit ' + str(row['exit_code']), flush=True)
        return row
    try:
        save(); assert stable()
        gate_output = DATA / (PREFIX + '-fixed-gate.json')
        row = phase('fixed-gate', [CP, gate_script, '--baseline', BASELINE / 'xlang3.exe', '--candidate', RELEASE / 'xlang3.exe', '--output', gate_output], 900)
        gate = read(gate_output)
        assert (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .10)
        assert set(gate['cases']) == set(gate_sources)
        assert all(value['source_sha256'] == gate_sources[key] for key, value in gate['cases'].items())
        record['fixed_gate'] = {'exit_code': row['exit_code'], 'status': gate['status'], 'passed': row['exit_code'] == 0 and gate['status'] == 'pass', 'output': gate_output.name, 'sha256': sha(gate_output)}
        save()
        env.update(PYTHONPATH=str(ROOT / 'benchmarks/diagnostics/pyperf_compat'), PYTHONIOENCODING='utf-8')
        official = {}
        for label, exe in (('cpython3147', CP), ('xlang3', RELEASE / 'xlang3.exe')):
            output = DATA / (PREFIX + '-' + label + '-official-fast.json')
            row = phase(label + '-official-gc', [CP, RUNNER, '--runtime', exe, '--benchmarks', 'gc_collect,gc_traversal', '--mode', 'fast', '--case-timeout', '300', '--dependency-site', SITE, '--output', output], 660)
            scores = {}
            if output.exists():
                doc = read(output)
                for benchmark in doc['benchmarks']:
                    name = benchmark.get('metadata', {}).get('name', doc.get('metadata', {}).get('name'))
                    values = [v for run in benchmark['runs'] for v in run.get('values', [])]
                    assert len(values) == 20 and all(v > 0 for v in values)
                    scores[name] = {'values': values, 'mean_seconds': statistics.mean(values), 'stddev_seconds': statistics.stdev(values)}
            official[label] = {'completed': row['exit_code'] == 0 and set(scores) == {'create_gc_cycles', 'gc_traversal'}, 'exit_code': row['exit_code'], 'scores': scores, 'output': output.name, 'sha256': sha(output) if output.exists() else None}
            record['official'] = official; save()
        record['status'] = 'gate_passed_original_gc_completed' if record['fixed_gate']['passed'] and all(v['completed'] for v in official.values()) else 'measurements_completed_gate_or_official_failed'
    except BaseException as error:
        record.update(status='validation_failed_or_invalid', error=repr(error))
    finally:
        record.update(terminal=True, hashes_unchanged=stable()); save()
    print(json.dumps({'status': record['status'], 'fixed_gate': record.get('fixed_gate'), 'official': record.get('official'), 'receipt_sha256': sha(out)}, indent=2), flush=True)
    return 0 if record['status'] == 'gate_passed_original_gc_completed' and record['hashes_unchanged'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
