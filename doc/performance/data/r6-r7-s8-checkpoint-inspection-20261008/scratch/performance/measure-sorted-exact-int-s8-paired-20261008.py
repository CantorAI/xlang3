"""Seven fixed pairs of the unchanged callback boundary diagnostic; unscored."""
import hashlib
import importlib.util
import json
import math
import os
import random
import statistics
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CONTROL = ROOT / 'build-repro/controls/class-constructor-c5-before-snapshot-r6-20261008'
INVENTORY = DATA / 'sorted-exact-int-s8-registered-source-20261008.json'
INVENTORY_SHA = 'f20c304e32b0874c929d22dc72018cbd6d5fd9d124844c1d66d8edcc6a231556'
FOCUS = DATA / 'sorted-exact-int-s8-registered-focused-20261008.json'
CHILD = ROOT / 'benchmarks/diagnostics/python_callback_boundary.py'
CHILD_SHA = 'f76136594ac8e64e5fb0d71a47eff3af286d5e12178a8acbaa7fe4c43a6ef84a'
WATCH = ROOT / 'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'
WATCH_SHA = '50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf'
PREFIX = 'sorted-exact-int-s8-paired-20261008'
CASES = ('loop', 'small_calls', 'branch_calls', 'sort_plain', 'sort_callback')
ORDERS = [('control', 'candidate') if i % 2 == 0 else ('candidate', 'control') for i in range(7)]
SHA = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
READ = lambda p: json.loads(Path(p).read_bytes())

def main():
    assert sys.version_info[:3] == (3, 14, 7)
    assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
    output = DATA / (PREFIX + '.json')
    assert not any(DATA.glob(PREFIX + '*'))
    assert SHA(INVENTORY) == INVENTORY_SHA and SHA(CHILD) == CHILD_SHA and SHA(WATCH) == WATCH_SHA
    inv, focus = READ(INVENTORY), READ(FOCUS)
    assert focus['terminal'] and focus['hashes_unchanged'] and focus['status'] == 'targeted_correctness_passed'
    assert len(focus['phases']) == 14 and all(r['passed'] for r in focus['phases'])
    assert inv['source_sha256'] == focus['source_sha256'] and focus['source_inventory_sha256'] == INVENTORY_SHA
    release_map = lambda: {p.relative_to(ROOT).as_posix(): SHA(p) for p in RELEASE.rglob('*') if p.is_file()}
    binaries = release_map()
    assert len(binaries) == 178 and binaries == focus['binaries_sha256']
    manifest = CONTROL / 'preserved-release-provenance.json'
    assert SHA(manifest) == '85910ee375f16c677153fc128fb17303868a849c124f5909d5ee03eeed764bdc'
    saved = READ(manifest)
    pins = {str(ROOT / p): h for p, h in {**inv['source_sha256'], **binaries}.items()}
    for base, mapping in [(CONTROL, saved['files_sha256']), (CONTROL / 'source-snapshot', saved['source_snapshot_sha256'])]:
        for p, h in mapping.items(): pins[str(base / p)] = h
    for p in (INVENTORY, FOCUS, CHILD, WATCH, manifest, Path(__file__)): pins[str(p)] = SHA(p)
    def stable(): return release_map() == binaries and all(Path(p).is_file() and SHA(p) == h for p, h in pins.items())
    assert stable()
    record = dict(status='running_unscored_paired_callback_boundary', terminal=False, scored=False,
        full_validated=False, source_inventory_sha256=INVENTORY_SHA, source_sha256=inv['source_sha256'],
        binaries_sha256=binaries, focused_receipt_sha256=SHA(FOCUS), controller_sha256=SHA(Path(__file__)),
        child_sha256=CHILD_SHA, pair_count=7, total_child_invocations=14,
        total_timed_case_invocations=210, warmup_case_invocations_per_child=5,
        timed_samples_per_case_per_child=3, values_trimmed=0, outliers_removed=0,
        decision_rule=dict(selected_case='sort_plain', minimum_median_ratio=1.10, minimum_lower_ci=1.0,
            bootstrap_resamples=50000, bootstrap_seed=20261008, strict_greater_than=True),
        started_utc=datetime.now(timezone.utc).isoformat(), pair_results=[], raw=[], idle_guards=[], hashes_before=pins)
    def save(): output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    def idle(label):
        rows = json.loads(subprocess.check_output(['powershell', '-NoProfile', '-Command',
            'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress']).decode('utf-8-sig'))
        if isinstance(rows, dict): rows = [rows]
        busy = [r for r in rows if r['ProcessId'] != os.getpid() and
            (r['Name'].lower().startswith(('python', 'xlang3')) or r['Name'].lower() in
             {'cl.exe','link.exe','ninja.exe','cmake.exe','ctest.exe','msbuild.exe','nmake.exe','clang-cl.exe','lld-link.exe'})]
        record['idle_guards'].append(dict(phase=label, busy=busy)); save(); assert not busy, busy
    spec = importlib.util.spec_from_file_location('sorting_timing_watch', WATCH)
    watcher = importlib.util.module_from_spec(spec); spec.loader.exec_module(watcher)
    environment = dict(os.environ, XLANG3_PYTHON_LIB='C:/Python/Python314/Lib', PYTHONIOENCODING='utf-8', PYTHONUNBUFFERED='1')
    environment.pop('PYTHONOPTIMIZE', None)
    try:
        for index, order in enumerate(ORDERS):
            pair = dict(pair_index=index, order=list(order), runs={}); record['pair_results'].append(pair)
            for role in order:
                label = f'pair-{index+1:02d}-{role}'
                idle('before-' + label); assert stable()
                exe = (CONTROL if role == 'control' else RELEASE) / 'xlang3.exe'
                cache = ROOT / 'scratch/performance' / ('pycache-' + PREFIX + '-' + label); assert not cache.exists()
                stdout = DATA / (PREFIX + '-' + label + '.stdout.log')
                stderr = DATA / (PREFIX + '-' + label + '.stderr.log')
                row = dict(runtime=role, pair_index=index, command=[str(exe), str(CHILD)],
                    stdout_log=stdout.name, stderr_log=stderr.name, timeout=False, passed=False)
                pair['runs'][role] = row; record['raw'].append(row); save()
                finish = watcher.start_timing_process_watch(PREFIX, label, row)
                child = None
                try:
                    with stdout.open('xb') as out, stderr.open('xb') as err:
                        child = subprocess.Popen(row['command'], cwd=ROOT, env=dict(environment, PYTHONPYCACHEPREFIX=str(cache)),
                            stdin=subprocess.DEVNULL, stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
                        row['pid'] = child.pid; save()
                        try: row['exit_code'] = child.wait(timeout=120)
                        except subprocess.TimeoutExpired:
                            row['timeout'] = True
                            subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)], capture_output=True, timeout=15)
                            if child.poll() is None: child.kill()
                            row['exit_code'] = child.wait(timeout=15)
                    assert row['exit_code'] == 0 and not row['timeout'] and stderr.read_bytes() == b''
                    times = {case: [] for case in CASES}; indices = {case: [] for case in CASES}
                    lines = stdout.read_text(encoding='utf-8-sig').splitlines(); assert len(lines) == 15
                    for line in lines:
                        marker, name, sample, count, value = line.split()
                        assert marker == 'callback_boundary' and name in times and int(count) == 50000
                        elapsed = float(value); assert math.isfinite(elapsed) and elapsed > 0
                        times[name].append(elapsed); indices[name].append(int(sample))
                    assert all(values == [0,1,2] for values in indices.values())
                    row['times'] = times; row['passed'] = True
                finally:
                    if child is not None and child.poll() is None:
                        subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)], capture_output=True, timeout=15)
                        if child.poll() is None: child.kill()
                        child.wait(timeout=15)
                    row['measurement_valid'] = finish()
                    row['passed'] = row['passed'] and row['measurement_valid']
                    row['stdout_sha256'] = SHA(stdout); row['stderr_sha256'] = SHA(stderr); save()
                idle('after-' + label); assert stable() and row['passed'], 'No retry or replacement sample'
            pair['ratios'] = {case: statistics.median(pair['runs']['control']['times'][case]) /
                statistics.median(pair['runs']['candidate']['times'][case]) for case in CASES}
            save()
        summaries = {}
        for case in CASES:
            ratios = [p['ratios'][case] for p in record['pair_results']]
            rng = random.Random(20261008)
            draws = sorted(statistics.median([ratios[rng.randrange(7)] for _ in range(7)]) for _ in range(50000))
            def percentile(q):
                i = q * (len(draws) - 1); lo = math.floor(i); hi = math.ceil(i)
                return draws[lo] + (draws[hi] - draws[lo]) * (i - lo)
            summaries[case] = dict(pair_ratios=ratios, median_pair_ratio=statistics.median(ratios),
                bootstrap95_ci=[percentile(.025), percentile(.975)])
        summary = dict(summaries['sort_plain'], all_cases=summaries)
        summary['useful_signal'] = summary['median_pair_ratio'] > 1.10 and summary['bootstrap95_ci'][0] > 1.0
        summary['decision'] = 'signal_for_full_checkpoint_validation' if summary['useful_signal'] else 'reject_sorting_trial'
        record.update(status='terminal_unscored_paired_callback_boundary', summary=summary)
    except BaseException as error:
        record.update(status='terminal_failed_paired_callback_boundary', error=type(error).__name__ + ': ' + str(error))
    finally:
        record['hashes_after'] = {p: SHA(p) if Path(p).is_file() else None for p in pins}
        record['hashes_unchanged'] = record['hashes_after'] == pins and release_map() == binaries
        if not record['hashes_unchanged']: record['status'] = 'terminal_invalid_hash_drift'
        record['terminal'] = True; record['completed_utc'] = datetime.now(timezone.utc).isoformat(); save()
    print(record['status'], record.get('summary', {}), flush=True)
    return 0 if record['status'] == 'terminal_unscored_paired_callback_boundary' else 1

if __name__ == '__main__': raise SystemExit(main())
