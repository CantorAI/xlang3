"""Alternating checked diagnostics, including unchanged callback controls."""
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
output = root / 'doc/performance/data/captured-lookup-paired-20261007.json'
assert not output.exists()
control = root / 'build-repro/controls/immutable-key-hash-checkpoint-20261007/xlang3.exe'
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
binaries = {label: {'exe': digest(path), 'dll': digest(path.with_name('xlang3_runtime.dll'))}
            for label, path in (('control', control), ('candidate', candidate))}
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
    env.pop(name, None)
record = {'status': 'running', 'scope': 'Checked diagnostics, not official scores', 'binaries_sha256': binaries, 'probes': []}

def identity(row):
    return {key: value for key, value in row.items() if key not in ('samples_seconds', 'checksums', 'checked_hash_calls')}

for label, filename, count, samples in (
    ('hash', 'immutable-key-hash-probe-20261007.py', 8, 5),
    ('update', 'dict-composite-scaling-probe-20261007.py', 12, 3),
    ('trivial_callback', 'native-trivial-callback-probe-20261007.py', 5, 5),
    ('nontrivial_callback', 'dict-callback-boundary-probe-20261007.py', 6, 5),
):
    probe = root / 'scratch/performance' / filename
    observed = {'name': label, 'filename': filename, 'probe_sha256': digest(probe), 'pairs': []}
    for pair_number in range(7):
        order = ('control', 'candidate') if pair_number % 2 == 0 else ('candidate', 'control')
        runs = {}
        for name in order:
            executable = control if name == 'control' else candidate
            result = subprocess.run([str(executable), str(probe)], cwd=root, env=env, capture_output=True, timeout=120)
            assert result.returncode == 0, result.stderr
            runs[name] = json.loads(result.stdout)
            assert len(runs[name]['rows']) == count
            for row in runs[name]['rows']:
                assert len(row['samples_seconds']) == samples
                if label == 'hash':
                    assert row['operations'] == 8192
                    assert row['checked_hash_calls'] == [8192 * row['hashes_per_key']] * samples
                elif label == 'update':
                    assert row['updates'] == row['key_count'] * 4
                    assert row['checksums'] == [row['updates']] * samples
                elif label == 'trivial_callback':
                    assert row['mapping_stays_empty'] and row['operations_per_sample'] == 8192
                    assert row['checked_result'] == (32 if row['path'] == 'native_max_constant_key' else 0)
                else:
                    assert row['checksum'] == 8 * 1023 and row['lookups_per_sample'] == 8192
        for before, after in zip(runs['control']['rows'], runs['candidate']['rows']):
            assert identity(before) == identity(after)
        observed['pairs'].append({'order': order, 'runs': runs})
        print('Finished', label, 'pair', pair_number + 1, flush=True)
    summaries = []
    for index, row in enumerate(observed['pairs'][0]['runs']['control']['rows']):
        ratios = [statistics.median(pair['runs']['control']['rows'][index]['samples_seconds']) /
                  statistics.median(pair['runs']['candidate']['rows'][index]['samples_seconds']) for pair in observed['pairs']]
        summaries.append({'identity': identity(row), 'control_time_over_candidate_time': ratios,
                          'median_speedup': statistics.median(ratios), 'pairs_favoring_candidate': sum(value > 1 for value in ratios)})
    observed['summary'] = summaries
    record['probes'].append(observed)
    output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(label, 'summary:', json.dumps(summaries), flush=True)
for label, path in (('control', control), ('candidate', candidate)):
    assert binaries[label] == {'exe': digest(path), 'dll': digest(path.with_name('xlang3_runtime.dll'))}
record['status'] = 'terminal'
output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
