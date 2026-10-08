"""Seven alternating checked callback diagnostics; not official suite scores."""
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
probe = root / 'scratch/performance/dict-callback-boundary-probe-20261007.py'
output = root / 'doc/performance/data/minmax-streaming-paired-callbacks-20261007.json'
assert not output.exists()
control = root / 'build-repro/controls/inherited-subscript-checkpoint-20261007/xlang3.exe'
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
binaries = {name: {'exe': digest(path), 'dll': digest(path.with_name('xlang3_runtime.dll'))}
            for name, path in (('control', control), ('candidate', candidate))}
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
    env.pop(name, None)
record = {'status': 'running', 'scope': 'Diagnostic only, not official BPE; checked checksums',
          'binaries_sha256': binaries, 'probe_sha256': digest(probe), 'pairs': []}
for pair in range(7):
    order = ('control', 'candidate') if pair % 2 == 0 else ('candidate', 'control')
    runs = {}
    for name in order:
        result = subprocess.run([str(control if name == 'control' else candidate), str(probe)],
                                cwd=root, env=env, capture_output=True, timeout=60)
        assert result.returncode == 0, result.stderr
        runs[name] = json.loads(result.stdout)
        assert len(runs[name]['rows']) == 6
        for row in runs[name]['rows']:
            assert row['checksum'] == 8 * 1023 and row['lookups_per_sample'] == 8192
            assert len(row['samples_seconds']) == 5
    record['pairs'].append({'order': order, 'runs': runs})
    output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print('Finished callback diagnostic pair', pair + 1, flush=True)
summary = []
for index, row in enumerate(record['pairs'][0]['runs']['control']['rows']):
    ratios = []
    for pair in record['pairs']:
        before, after = (pair['runs'][name]['rows'][index] for name in ('control', 'candidate'))
        assert (before['mapping'], before['path']) == (after['mapping'], after['path'])
        ratios.append(statistics.median(before['samples_seconds']) / statistics.median(after['samples_seconds']))
    summary.append({'mapping': row['mapping'], 'path': row['path'],
                    'control_time_over_candidate_time': ratios, 'median_speedup': statistics.median(ratios),
                    'pairs_favoring_candidate': sum(ratio > 1 for ratio in ratios)})
for name, path in (('control', control), ('candidate', candidate)):
    assert binaries[name] == {'exe': digest(path), 'dll': digest(path.with_name('xlang3_runtime.dll'))}
record.update(status='terminal', summary=summary)
output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print('Callback diagnostic terminal:', json.dumps(summary), flush=True)
