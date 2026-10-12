"""Checked alternating diagnosis, including unchanged nontrivial callbacks."""
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
output = root / 'doc/performance/data/dict-missing-special-lookup-paired-20261007.json'
assert not output.exists()
control = root / 'build-repro/controls/native-trivial-callback-checkpoint-20261007/xlang3.exe'
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
binaries = {name: {'exe': digest(path), 'dll': digest(path.with_name('xlang3_runtime.dll'))}
            for name, path in (('control', control), ('candidate', candidate))}
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
    env.pop(name, None)
record = {'status': 'running', 'scope': 'Checked paired diagnosis only, never official scores',
          'binaries_sha256': binaries, 'probes': []}
for label, filename, row_count in (('trivial', 'native-trivial-callback-probe-20261007.py', 5),
                                  ('nontrivial', 'dict-callback-boundary-probe-20261007.py', 6)):
    probe = root / 'scratch/performance' / filename
    observed = {'name': label, 'probe_sha256': digest(probe), 'pairs': []}
    for pair in range(7):
        order = ('control', 'candidate') if pair % 2 == 0 else ('candidate', 'control')
        runs = {}
        for name in order:
            result = subprocess.run([str(control if name == 'control' else candidate), str(probe)],
                                    cwd=root, env=env, capture_output=True, timeout=60)
            assert result.returncode == 0, result.stderr
            runs[name] = json.loads(result.stdout)
            assert len(runs[name]['rows']) == row_count
            for row in runs[name]['rows']:
                assert len(row['samples_seconds']) == 5
                if label == 'trivial':
                    assert row['mapping_stays_empty'] and row['operations_per_sample'] == 8192
                    assert row['checked_result'] == (32 if row['path'] == 'native_max_constant_key' else 0)
                else:
                    assert row['checksum'] == 8 * 1023 and row['lookups_per_sample'] == 8192
        observed['pairs'].append({'order': order, 'runs': runs})
        print('Finished', label, 'pair', pair + 1, flush=True)
    summary = []
    for index, row in enumerate(observed['pairs'][0]['runs']['control']['rows']):
        ratios = []
        for pair in observed['pairs']:
            before, after = (pair['runs'][name]['rows'][index] for name in ('control', 'candidate'))
            assert before['path'] == after['path']
            assert before.get('mapping') == after.get('mapping')
            ratios.append(statistics.median(before['samples_seconds']) / statistics.median(after['samples_seconds']))
        summary.append({'path': row['path'], 'mapping': row.get('mapping'),
                        'control_time_over_candidate_time': ratios, 'median_speedup': statistics.median(ratios),
                        'pairs_favoring_candidate': sum(value > 1 for value in ratios)})
    observed['summary'] = summary
    record['probes'].append(observed)
    output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(label, 'summary:', json.dumps(summary), flush=True)
for name, path in (('control', control), ('candidate', candidate)):
    assert binaries[name] == {'exe': digest(path), 'dll': digest(path.with_name('xlang3_runtime.dll'))}
record['status'] = 'terminal'
output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
