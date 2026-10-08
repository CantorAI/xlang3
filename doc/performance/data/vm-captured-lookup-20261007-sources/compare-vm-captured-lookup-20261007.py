"""Early alternating call diagnostics; unchanged probes, no official score."""
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
output = root / 'doc/performance/data/vm-captured-lookup-paired-20261007.json'
assert not output.exists()
executables = {'control': root / 'build-repro/controls/captured-lookup-checkpoint-20261007/xlang3.exe',
               'candidate': root / 'build-repro/main-verify-20261006/Release/xlang3.exe'}
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
binary_hashes = lambda: {label: {'exe': digest(path), 'dll': digest(path.with_name('xlang3_runtime.dll'))}
                         for label, path in executables.items()}
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
    env.pop(name, None)
record = {'status': 'running', 'scope': 'Checked diagnostic, not an official score',
          'binaries_sha256': binary_hashes(), 'probes': []}
identity = lambda row: {key: value for key, value in row.items() if key != 'samples_seconds'}
for label, filename, count in (('trivial_callback', 'native-trivial-callback-probe-20261007.py', 5),
                               ('nontrivial_callback', 'dict-callback-boundary-probe-20261007.py', 6)):
    probe = root / 'scratch/performance' / filename
    observed = {'name': label, 'filename': filename, 'probe_sha256': digest(probe), 'pairs': []}
    for number in range(7):
        order = ('control', 'candidate') if number % 2 == 0 else ('candidate', 'control')
        runs = {}
        for name in order:
            result = subprocess.run([str(executables[name]), str(probe)], cwd=root, env=env,
                                    capture_output=True, timeout=120)
            assert result.returncode == 0, result.stderr
            runs[name] = json.loads(result.stdout)
            assert len(runs[name]['rows']) == count
            for row in runs[name]['rows']:
                assert len(row['samples_seconds']) == 5
                if label == 'trivial_callback':
                    assert row['mapping_stays_empty'] and row['operations_per_sample'] == 8192
                    assert row['checked_result'] == (32 if row['path'] == 'native_max_constant_key' else 0)
                else:
                    assert row['checksum'] == 8 * 1023 and row['lookups_per_sample'] == 8192
        assert [identity(row) for row in runs['control']['rows']] == [identity(row) for row in runs['candidate']['rows']]
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
assert record['binaries_sha256'] == binary_hashes()
record['status'] = 'terminal'
output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
