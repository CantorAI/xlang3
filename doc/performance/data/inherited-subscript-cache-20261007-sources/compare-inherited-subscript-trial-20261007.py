"""Seven alternating control/candidate diagnostic pairs, never suite scores."""
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
probe = root / 'scratch/performance/inherited-subscript-dispatch-probe-20261007.py'
out = root / 'doc/performance/data/inherited-subscript-cache-paired-20261007.json'
assert not out.exists()
control = root / 'build-repro/controls/dict-intrinsic-index-checkpoint-20261007/xlang3.exe'
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
binaries = {name: {'exe': digest(path), 'dll': digest(path.with_name('xlang3_runtime.dll'))}
            for name, path in (('control', control), ('candidate', candidate))}
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
    env.pop(name, None)
record = {'status': 'running', 'scope': 'diagnostic only, not official BPE; all values checked',
          'binaries_sha256': binaries, 'probe_sha256': digest(probe), 'pairs': []}
for pair in range(7):
    order = ('control', 'candidate') if pair % 2 == 0 else ('candidate', 'control')
    runs = {}
    for name in order:
        result = subprocess.run([str(control if name == 'control' else candidate), str(probe)],
                                cwd=root, env=env, capture_output=True, timeout=60)
        assert result.returncode == 0, result.stderr.decode('utf-8', errors='replace')
        runs[name] = json.loads(result.stdout)
    assert [row['path'] for row in runs['control']['rows']] == [row['path'] for row in runs['candidate']['rows']]
    record['pairs'].append({'order': order, 'runs': runs})
    out.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print('Completed diagnostic pair', pair + 1, flush=True)
summary = []
for index, row in enumerate(record['pairs'][0]['runs']['control']['rows']):
    ratios = []
    for pair in record['pairs']:
        left = pair['runs']['control']['rows'][index]
        right = pair['runs']['candidate']['rows'][index]
        assert all(item['all_values_verified'] for item in (left, right))
        assert (left['path'], left['key_count'], left['updates']) == (right['path'], right['key_count'], right['updates'])
        ratios.append(statistics.median(left['samples_seconds']) / statistics.median(right['samples_seconds']))
    summary.append({'path': row['path'], 'key_count': row['key_count'],
                    'control_time_divided_by_candidate_time': ratios,
                    'median_speedup': statistics.median(ratios),
                    'pairs_favoring_candidate': sum(ratio > 1 for ratio in ratios)})
record['summary'] = summary
record['status'] = 'terminal'
assert binaries == {name: {'exe': digest(path), 'dll': digest(path.with_name('xlang3_runtime.dll'))}
                    for name, path in (('control', control), ('candidate', candidate))}
out.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
for row in summary:
    if row['key_count'] == 1024:
        print(row['path'], 'speedup', round(row['median_speedup'], 4),
              'favorable pairs', row['pairs_favoring_candidate'], '/7', flush=True)
