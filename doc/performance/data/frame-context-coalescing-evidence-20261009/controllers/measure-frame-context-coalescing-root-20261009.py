"""Serial balanced call diagnostic or official unpickle A/B screen; keep all values."""
import argparse
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
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CONTROL = ROOT / 'build-repro/controls/trace-disable-local-events-accepted-20261009'
BASELINE = ROOT / 'build-repro/Release'
APP = DATA / 'frame-context-coalescing-applied-source-20261009.json'
CORRECT = DATA / 'frame-context-coalescing-correctness-20261009.json'
WATCH = ROOT / 'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'
CHILD = ROOT / 'benchmarks/diagnostics/python_callback_boundary.py'
RUNNER = ROOT / 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py'
SITE = ROOT / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(p.read_bytes())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=['diagnostic', 'official'], required=True)
    args = parser.parse_args()
    assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7)
    assert sha(APP) == 'b44fd6e9c7c0df388cc9d463ccba69bbffa766bc17c927038c36ddfecb4a0c9a'
    assert sha(WATCH) == '50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf'
    app, correct = read(APP), read(CORRECT)
    control = read(CONTROL / 'preserved-release-provenance.json')
    assert correct['terminal'] and correct['correctness_passed'] and correct['sources_unchanged']
    assert correct['release_unchanged'] and correct['fixed_baseline_unchanged']
    assert app['source_count'] == correct['source_count'] == 140
    prefix = 'frame-context-coalescing-' + args.mode + '-20261009'
    assert not any(DATA.glob(prefix + '*'))
    out = DATA / (prefix + '.json')
    pins = {str(ROOT / p): h for p, h in correct['source_sha256'].items()}
    for folder, mapping in ((RELEASE, correct['release_sha256']),
        (CONTROL / 'Release', control['files_sha256']),
        (CONTROL / 'sources', control['source_snapshot_sha256']),
        (BASELINE, app['fixed_baseline_sha256'])):
        pins.update({str(folder / p): h for p, h in mapping.items()})
    pins.update({str(ROOT / p): h for p, h in app['unowned_tracked_dirty_sha256'].items()})
    for p in (APP, CORRECT, WATCH, CHILD, RUNNER, Path(__file__), CONTROL / 'preserved-release-provenance.json', CP, CP.with_name('python314.dll')):
        pins[str(p)] = sha(p)
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    assert head == app['head']
    record = {'terminal': False, 'status': 'running', 'mode': args.mode, 'phases': [], 'idle_guards': [],
        'pins_before': pins, 'head': head, 'correctness_sha256': sha(CORRECT),
        'scope': 'Diagnostic: unchanged branching calls and sorted native callbacks, six permutations of CP/control/candidate. Official: unmodified unpickle_pure_python, fast-mode control/candidate ABBA, all values retained; no full-suite score or projected gain.',
        'baseline_replaced': False, 'gate_passed': False, 'engine_commit_permitted': False}
    def save():
        out.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
    def stable():
        return all(Path(p).is_file() and sha(p) == h for p, h in pins.items())
    def idle(label):
        raw = subprocess.check_output(['powershell', '-NoProfile', '-Command', 'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'])
        rows = json.loads(raw.decode('utf-8-sig') or '[]')
        if isinstance(rows, dict): rows = [rows]
        tools = {'cl.exe', 'link.exe', 'ninja.exe', 'msbuild.exe', 'cmake.exe', 'ctest.exe', 'nmake.exe', 'clang-cl.exe', 'lld-link.exe'}
        busy = [p for p in rows if p['ProcessId'] != os.getpid() and (p['Name'].lower() in tools or p['Name'].lower().startswith(('python', 'xlang3')))]
        record['idle_guards'].append({'phase': label, 'busy': busy}); save()
        assert not busy, busy
    spec = importlib.util.spec_from_file_location('frame_trial_watch', WATCH)
    watcher = importlib.util.module_from_spec(spec); spec.loader.exec_module(watcher)
    executables = {'cpython3147': CP, 'control': CONTROL / 'Release/xlang3.exe', 'candidate': RELEASE / 'xlang3.exe'}
    def phase(name, runtime, command, timeout):
        assert stable(); idle('before-' + name)
        row = {'name': name, 'runtime': runtime, 'command': [str(c) for c in command]}
        record['phases'].append(row); save()
        env = os.environ.copy()
        for key in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE', 'PYTHONHOME', 'XLANG3_VM_OPCODE_TIMING'):
            env.pop(key, None)
        env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
        env['PATH'] = str(executables[runtime].parent) + os.pathsep + env.get('PATH', '')
        if args.mode == 'official':
            env.update(PYTHONPATH=str(ROOT / 'benchmarks/diagnostics/pyperf_compat'), PYTHONIOENCODING='utf-8')
        stdout, stderr = (DATA / (prefix + '-' + name + suffix) for suffix in ('.stdout.log', '.stderr.log'))
        finish = watcher.start_timing_process_watch(prefix, name, row)
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
        assert stable() and row['measurement_valid'] and row['exit_code'] == 0
        print(name + ': PASS', flush=True)
        return row, stdout
    try:
        save(); assert stable()
        if args.mode == 'diagnostic':
            orders = list(itertools.permutations(executables))
            record['orders'] = orders
            for block, order in enumerate(orders):
                for runtime in order:
                    exe = executables[runtime]
                    extra = ['-I'] if runtime == 'cpython3147' else []
                    row, stdout = phase('block-%02d-%s' % (block, runtime), runtime, [exe, *extra, CHILD], 180)
                    samples = {}
                    for line in stdout.read_text(encoding='utf-8').splitlines():
                        tag, name, index, count, duration = line.split()
                        assert tag == 'callback_boundary' and int(count) == 50000
                        samples.setdefault(name, []).append(float(duration))
                    assert set(samples) == {'loop', 'small_calls', 'branch_calls', 'sort_plain', 'sort_callback'}
                    assert all(len(v) == 3 and all(x > 0 for x in v) for v in samples.values())
                    row.update(block=block, samples=samples); save()
            record['medians_seconds'] = {runtime: {case: statistics.median([v for p in record['phases'] if p['runtime'] == runtime for v in p['samples'][case]]) for case in record['phases'][0]['samples']} for runtime in executables}
            record['control_over_candidate_by_block'] = {case: [statistics.mean(next(p for p in record['phases'] if p['block'] == block and p['runtime'] == 'control')['samples'][case]) / statistics.mean(next(p for p in record['phases'] if p['block'] == block and p['runtime'] == 'candidate')['samples'][case]) for block in range(6)] for case in record['phases'][0]['samples']}
        else:
            for index, runtime in enumerate(('control', 'candidate', 'candidate', 'control')):
                name = 'abba-%d-%s' % (index, runtime)
                output = DATA / (prefix + '-' + name + '.json')
                row, _ = phase(name, runtime, [CP, RUNNER, '--runtime', executables[runtime], '--benchmarks', 'unpickle_pure_python', '--mode', 'fast', '--case-timeout', '300', '--dependency-site', SITE, '--output', output], 360)
                doc = read(output)
                assert len(doc['benchmarks']) == 1
                values = [v for run in doc['benchmarks'][0]['runs'] for v in run.get('values', [])]
                assert len(values) == 20 and all(v > 0 for v in values)
                row.update(output=output.name, output_sha256=sha(output), values=values, mean_seconds=statistics.mean(values)); save()
            rows = record['phases']
            record['control_over_candidate_by_pair'] = [rows[0]['mean_seconds'] / rows[1]['mean_seconds'], rows[3]['mean_seconds'] / rows[2]['mean_seconds']]
        record['status'] = 'measurement_completed'
    except BaseException as error:
        record.update(status='measurement_failed_or_invalid', error=repr(error))
    finally:
        record.update(terminal=True, hashes_unchanged=stable()); save()
    print(json.dumps({'status': record['status'], 'receipt_sha256': sha(out), 'summary': record.get('medians_seconds', record.get('control_over_candidate_by_pair'))}, indent=2), flush=True)
    return 0 if record['status'] == 'measurement_completed' and record['hashes_unchanged'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
