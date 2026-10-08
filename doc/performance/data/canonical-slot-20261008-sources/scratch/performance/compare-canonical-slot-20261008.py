"""Early paired slot/unchanged SQLGlot diagnostics, preserving every row."""
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
output = data / 'canonical-slot-paired-20261008.json'
assert not output.exists()
executables = {'control': root / 'build-repro/controls/vm-captured-lookup-checkpoint-20261007/xlang3.exe',
               'candidate': root / 'build-repro/main-verify-20261006/Release/xlang3.exe'}
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
binaries = lambda: {label: {'exe': digest(path), 'dll': digest(path.with_name('xlang3_runtime.dll'))}
                    for label, path in executables.items()}
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
hook = root / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
env['PYTHONPATH'] = os.pathsep.join((str(hook.parent), str(root / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages')))
env['PYTHONIOENCODING'] = 'utf-8'
env.pop('PYTHONPYCACHEPREFIX', None)
source = Path(r'C:\Python\Python314\Lib\site-packages\pyperformance\data-files\benchmarks\bm_sqlglot_v2\run_benchmark.py')
record = {'status': 'running', 'scope': 'Diagnostic including unchanged original SQLGlot body; not official scores',
          'binaries_sha256': binaries(), 'benchmark_source_sha256': digest(source),
          'compatibility_hook_sha256': digest(hook), 'probes': []}
identity = lambda row: {key: value for key, value in row.items() if key != 'samples_seconds'}
for label, filename, count in (('slot', 'canonical-slot-probe-20261008.py', 3),
                               ('sqlglot_parse', 'canonical-slot-sqlglot-probe-20261008.py', 1)):
    probe = root / 'scratch/performance' / filename
    observed = {'name': label, 'filename': filename, 'probe_sha256': digest(probe), 'pairs': []}
    reference = subprocess.run([sys.executable, str(probe)], cwd=root, env=env, capture_output=True, timeout=120)
    assert reference.returncode == 0, reference.stdout + reference.stderr
    observed['cpython3147'] = json.loads(reference.stdout)
    for number in range(7):
        order = ('control', 'candidate') if number % 2 == 0 else ('candidate', 'control')
        runs = {}
        for name in order:
            completed = subprocess.run([str(executables[name]), str(probe)], cwd=root, env=env,
                                       capture_output=True, timeout=120)
            assert completed.returncode == 0, completed.stdout + completed.stderr
            runs[name] = json.loads(completed.stdout)
            assert len(runs[name]['rows']) == count
            for row in runs[name]['rows']:
                assert len(row['samples_seconds']) == 5
                if label == 'slot':
                    assert row['operations'] == 16384 and row['checksum'] == 16384 * 7
                else:
                    assert row['parses_per_sample'] == 10
        assert [identity(row) for row in runs['control']['rows']] == [identity(row) for row in runs['candidate']['rows']]
        assert [identity(row) for row in runs['candidate']['rows']] == [identity(row) for row in observed['cpython3147']['rows']]
        observed['pairs'].append({'order': order, 'runs': runs})
        print('Finished', label, 'pair', number + 1, flush=True)
    observed['summary'] = []
    for index, row in enumerate(observed['pairs'][0]['runs']['control']['rows']):
        ratios = [statistics.median(pair['runs']['control']['rows'][index]['samples_seconds']) /
                  statistics.median(pair['runs']['candidate']['rows'][index]['samples_seconds'])
                  for pair in observed['pairs']]
        observed['summary'].append({'identity': identity(row), 'control_time_over_candidate_time': ratios,
                                    'median_speedup': statistics.median(ratios),
                                    'pairs_favoring_candidate': sum(value > 1 for value in ratios)})
    record['probes'].append(observed)
    output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(label, 'summary:', json.dumps(observed['summary']), flush=True)
assert record['binaries_sha256'] == binaries() and record['benchmark_source_sha256'] == digest(source)
record['status'] = 'terminal'
output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
