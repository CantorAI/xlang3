"""Held three alternating pairs of the unchanged original official sqlite_synth.

Root executes only after candidate validation is terminal. CPython 3.14.7 is
the manager, not a timed reference. No benchmark body replacement or retry.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.metadata
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
CANDIDATE = ROOT / 'build-repro/main-verify-20261006/Release/xlang3.exe'
CONTROL = ROOT / 'build-repro/controls/native-bound-zero-args-checkpoint-20261008/xlang3.exe'
RUNNER = ROOT / 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py'
HOOK = ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
PARTIAL_HELPER = RUNNER.with_name('preserve_pyperformance_partial.py')
CP_CONFIG = DATA / 'pyperformance-cpython3147-live-eval-full-fast-20261007-provenance.json'
ORIGINAL = CP.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks/bm_sqlite_synth/run_benchmark.py'
ORIGINAL_SHA = 'dcba7a6889e66f08480f4332860208d624678fc42a03be5c8f3b6f1f894231fd'


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def statistics_for(values):
    mean = statistics.fmean(values)
    sd = statistics.stdev(values)
    return dict(mean_seconds=mean, sd_seconds=sd, cv_percent=100 * sd / mean,
                median_seconds=statistics.median(values),
                min_seconds=min(values), max_seconds=max(values))


def inputs(directory):
    return {p.relative_to(directory).as_posix(): digest(p) for p in sorted(directory.rglob('*'))
            if p.is_file() and '__pycache__' not in p.parts and p.suffix not in ('.pyc', '.pyo')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validation', type=Path, required=True)
    parser.add_argument('--source-inventory', type=Path,
                        default=DATA / 'sqlite-statement-cache-r4-compiled-source-20261008.json')
    parser.add_argument('--prefix', default='sqlite-statement-cache-r4-original-paired-20261008')
    args = parser.parse_args()
    assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CP.resolve()
    assert re.fullmatch(r'[a-z0-9-]+', args.prefix)
    assert not any(DATA.glob(args.prefix + '*')), 'Use a fresh prefix; earlier evidence is immutable'
    output = DATA / (args.prefix + '.json')
    validation_path, inventory_path = (p.resolve(strict=True) for p in (args.validation, args.source_inventory))
    validation, inventory = read(validation_path), read(inventory_path)
    assert validation['status'] == 'validated' and validation['terminal'] and validation['hashes_unchanged']
    assert validation['correctness_passed'] and validation['fixed_gate']['exit_code'] == 0
    gate_path = DATA / validation['fixed_gate']['output']
    assert digest(gate_path) == validation['fixed_gate']['sha256']
    assert validation['source_inventory_sha256'] == digest(inventory_path)
    sources = inventory['source_sha256']
    assert len(sources) == 44 and sources == validation['source_sha256']
    assert all(re.fullmatch(r'[0-9a-f]{64}', value) for value in sources.values())
    cp = read(CP_CONFIG)
    assert cp['runtime_version'] == '3.14.7' and Path(cp['runtime_executable']).resolve() == CP.resolve()
    assert cp['sha256_start'] == cp['sha256_end'] and digest(CP) == cp['sha256_end']['exe']
    assert digest(CP.with_name('python314.dll')) == cp['sha256_end']['dll']
    assert digest(HOOK) == cp['compatibility_hook_sha256'] and digest(RUNNER) == cp['runner_sha256']
    assert importlib.metadata.version('pyperformance') == cp['pyperformance_version']
    assert importlib.metadata.version('pyperf') == cp['pyperf_version']
    site = Path(cp['dependency_site']).resolve(strict=True)
    metadata = {p.relative_to(site).as_posix(): digest(p) for p in sorted(site.glob('*.dist-info/METADATA'))}
    assert metadata == {name.replace('\\', '/'): value for name, value in cp['dependency_metadata_sha256'].items()}
    assert digest(ORIGINAL) == ORIGINAL_SHA
    manifest_path = CONTROL.with_name('preserved-release-provenance.json')
    manifest = read(manifest_path)
    assert manifest['accepted'] and len(manifest['files_sha256']) == 140
    record = dict(status='running', terminal=False, pid=os.getpid(),
        started_utc=datetime.now(timezone.utc).isoformat(), pair_count=3, samples_per_run=20,
        benchmark='sqlite_synth', mode='fast', timed_runtimes=['control', 'candidate'],
        no_cpython_rerun=True, scope='Original official body; three serial alternating pairs, no retry or trimmed values',
        ratio_definition='Within each pair: mean(control 20 values) / mean(candidate 20 values); summary is median of three pair ratios',
        validation=str(validation_path), validation_sha256=digest(validation_path),
        fixed_gate=str(gate_path), fixed_gate_sha256=digest(gate_path),
        source_inventory=str(inventory_path), source_inventory_sha256=digest(inventory_path),
        source_sha256=sources, control_manifest=str(manifest_path), control_manifest_sha256=digest(manifest_path),
        runtime_executables={'control': str(CONTROL), 'candidate': str(CANDIDATE)},
        manager_executable=str(CP), manager_version=sys.version, manager_configuration_provenance=str(CP_CONFIG),
        manager_configuration_provenance_sha256=digest(CP_CONFIG),
        dependency_site=str(site), compatibility_hook_sha256=digest(HOOK), runner_sha256=digest(RUNNER),
        original_source=str(ORIGINAL), original_source_sha256=ORIGINAL_SHA,
        runs=[], paired_rows=[], idle_guards=[], raw_stream_policy='stdout+stderr merged into one binary file, no text conversion')
    tracked = {}

    def track(path, expected=None):
        path = Path(path).resolve(strict=True)
        sha = digest(path)
        if expected is not None: assert sha == expected, str(path)
        assert str(path) not in tracked or tracked[str(path)] == sha
        tracked[str(path)] = sha

    for path in (Path(__file__), validation_path, gate_path, inventory_path, manifest_path, CP_CONFIG,
                 CP, CP.with_name('python314.dll'), HOOK, RUNNER, PARTIAL_HELPER, ORIGINAL):
        track(path)
    for path, expected in sources.items(): track(ROOT / path, expected)
    for path, expected in validation['binaries_sha256'].items(): track(ROOT / path, expected)
    binary_before = {}
    for role, executable in (('control', CONTROL), ('candidate', CANDIDATE)):
        binary_before[role] = {}
        for relative, expected in manifest['files_sha256'].items():
            path = executable.parent / relative
            track(path, expected if role == 'control' else None)
            binary_before[role][relative] = digest(path)
        for required in ('xlang3.exe', 'xlang3_runtime.dll', 'modules\\xlang_sqlite3.x3pkg.dll'):
            assert required in binary_before[role], (role, 'missing pinned critical native binary', required)
    record['binaries_sha256_start'] = binary_before
    record['tracked_sha256_before'] = dict(tracked)
    benchmark_inputs = inputs(ORIGINAL.parent)
    dependency_inputs = inputs(site)
    record['benchmark_inputs_sha256'] = benchmark_inputs
    record['dependency_inputs_sha256'] = dependency_inputs

    def save():
        output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))

    def unchanged():
        assert all(Path(path).is_file() and digest(path) == expected for path, expected in tracked.items()), 'Source/binary/tool drift'

    def idle(phase):
        command = 'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'
        result = subprocess.run(['powershell', '-NoProfile', '-Command', command], capture_output=True, check=True)
        rows = json.loads(result.stdout.decode('utf-8-sig') or '[]')
        if isinstance(rows, dict): rows = [rows]
        tools = {'cl.exe','link.exe','ninja.exe','msbuild.exe','cmake.exe','ctest.exe',
                 'nmake.exe','lld-link.exe','clang-cl.exe'}
        busy = [row for row in rows if row['ProcessId'] != os.getpid() and
                (row['Name'].lower() in tools or row['Name'].lower().startswith(('python','xlang3')))]
        record['idle_guards'].append(dict(phase=phase, allowed_controller_pid=os.getpid(), busy=busy))
        save()
        assert not busy, busy

    env = os.environ.copy()
    assert not env.get('XLANG3_VM_OPCODE_TIMING'), 'Official timings must be uninstrumented'
    for name in ('PYTHONPATH','PYTHONPYCACHEPREFIX','PYTHONIOENCODING'):
        env.pop(name, None)
    env.update(XLANG3_PYTHON_LIB=str(CP.parent / 'Lib'), PYTHONPATH=str(HOOK.parent), PYTHONIOENCODING='utf-8')
    child = None
    valid = False
    try:
        idle('before-pairs')
        for pair in range(1, 4):
            order = ['control','candidate'] if pair % 2 else ['candidate','control']
            for role in order:
                idle(f'before-pair-{pair}-{role}')
                unchanged()
                stem = f'{args.prefix}-pair-{pair}-{role}'
                json_path, log_path = (DATA / (stem + suffix) for suffix in ('.json','.log'))
                assert not json_path.exists() and not log_path.exists()
                executable = CONTROL if role == 'control' else CANDIDATE
                command = [str(CP), str(RUNNER), '--runtime', str(executable),
                    '--benchmarks','sqlite_synth','--mode','fast','--case-timeout','300',
                    '--dependency-site',str(site),'--output',str(json_path)]
                row = dict(pair=pair, role=role, order=order, command=command,
                    output=json_path.name, log=log_path.name, started_utc=datetime.now(timezone.utc).isoformat())
                record['runs'].append(row)
                save()
                run_env = env.copy()
                run_env['PYTHONPYCACHEPREFIX'] = str(ROOT / 'scratch/performance' / ('pycache-' + stem))
                print(f'Pair {pair}/3 starting {role}: original sqlite_synth --fast', flush=True)
                with log_path.open('xb') as log:
                    child = subprocess.Popen(command, cwd=ROOT, env=run_env, stdout=log, stderr=subprocess.STDOUT)
                    row['manager_pid'] = child.pid
                    save()
                    row['exit_code'] = child.wait(timeout=360)
                row.update(completed_utc=datetime.now(timezone.utc).isoformat(), log_sha256=digest(log_path),
                           output_sha256=digest(json_path) if json_path.is_file() else None)
                save()
                assert row['exit_code'] == 0 and json_path.is_file(), row
                document = read(json_path)
                benchmarks = document['benchmarks']
                assert len(benchmarks) == 1
                benchmark = benchmarks[0]
                meta = {**document.get('metadata', {}), **benchmark.get('metadata', {})}
                assert meta['name'] == 'sqlite_synth'
                values = [value for run in benchmark['runs'] for value in run.get('values', [])]
                assert len(values) == 20 and all(math.isfinite(value) and value > 0 for value in values)
                row.update(values_seconds=values, stats=statistics_for(values), benchmark_metadata=meta,
                    instability_warning='WARNING: the benchmark result may be unstable' in log_path.read_text(encoding='utf-8', errors='replace'))
                idle(f'after-pair-{pair}-{role}')
                unchanged()
                save()
                print(f"Pair {pair}/3 {role}: {row['stats']['mean_seconds']*1e6:.3f} us; CV {row['stats']['cv_percent']:.2f}%", flush=True)
            a, b = [next(row for row in record['runs'] if row['pair'] == pair and row['role'] == role)
                    for role in ('control','candidate')]
            ratio = a['stats']['mean_seconds'] / b['stats']['mean_seconds']
            record['paired_rows'].append(dict(pair=pair, order=order,
                control_mean_seconds=a['stats']['mean_seconds'], candidate_mean_seconds=b['stats']['mean_seconds'],
                control_over_candidate_speed=ratio))
            save()
            print(f'Pair {pair}/3 completed: control/candidate {ratio:.5f}x', flush=True)
        unchanged()
        assert inputs(ORIGINAL.parent) == benchmark_inputs and inputs(site) == dependency_inputs, 'Benchmark/dependency byte drift'
        ratios = [row['control_over_candidate_speed'] for row in record['paired_rows']]
        record['summary'] = dict(median_pair_speed=statistics.median(ratios), pair_ratios=ratios,
            candidate_faster_pairs=sum(ratio > 1 for ratio in ratios), pairs=3,
            raw_values_per_runtime=60, values_trimmed=0, outliers_removed=0)
        record['status'] = 'validated_paired_original_official'
        valid = True
    except BaseException as error:
        record.update(status='failed_or_invalid_paired_original_official', error=repr(error))
        if child is not None and child.poll() is None:
            try:
                subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)], capture_output=True, timeout=10, check=False)
                if child.poll() is None: child.kill()
                child.wait(timeout=10)
            except Exception as cleanup_error: record['cleanup_error'] = repr(cleanup_error)
    finally:
        after = {path: digest(path) if Path(path).is_file() else None for path in tracked}
        record.update(terminal=True, completed_utc=datetime.now(timezone.utc).isoformat(),
            tracked_sha256_after=after, hashes_unchanged=after == tracked)
        record['binaries_sha256_end'] = {role: {relative: digest((CONTROL if role == 'control' else CANDIDATE).parent / relative)
            if ((CONTROL if role == 'control' else CANDIDATE).parent / relative).is_file() else None
            for relative in rows} for role, rows in binary_before.items()}
        record['partial_evidence_sha256'] = {p.relative_to(DATA).as_posix(): digest(p)
            for p in sorted(DATA.glob(args.prefix + '*-partial/**/*')) if p.is_file()}
        if not record['hashes_unchanged']:
            record['status'] = 'invalid_hash_drift'
            valid = False
        for row in record['runs']:
            for field in ('log', 'output'):
                path = DATA / row[field]
                row[field + '_sha256'] = digest(path) if path.is_file() else None
        save()
    print('Terminal paired original SQLite:', record['status'],
          record.get('summary', {}).get('median_pair_speed'), flush=True)
    return 0 if valid else 2


if __name__ == '__main__':
    raise SystemExit(main())
