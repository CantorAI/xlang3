"""Export completed diagnostic phases; do not read or score pending BPE data."""
import csv
import hashlib
import json
from pathlib import Path
import statistics

root = Path.cwd()
data = root / 'doc/performance/data'
baseline_path = data / 'dict-composite-scaling-baseline-20261007.json'
baseline = json.loads(baseline_path.read_text(encoding='utf-8'))
sources = [('cpython3147', baseline_path, baseline['observations'][0]['parsed']['rows']),
           ('earlier_xlang3', baseline_path, baseline['observations'][1]['parsed']['rows'])]
assert [row['runtime'] for row in baseline['observations']] == ['cpython3147', 'xlang3']
assert all(row['exit_code'] == 0 for row in baseline['observations'])
probe_hash = baseline['probe_sha256']
for label, filename in (
        ('write_index', 'dict-native-key-index-validation-r2-20261007.json'),
        ('read_index', 'dict-native-read-index-validation-20261007.json'),
        ('finalizer_safe_index', 'dict-native-index-final-validation-20261007.json')):
    path = data / filename
    record = json.loads(path.read_text(encoding='utf-8'))
    assert record['scaling_probe_sha256'] == probe_hash
    assert next(phase for phase in record['phases'] if phase['name'] == 'scaling')['exit_code'] == 0
    sources.append((label, path, record['scaling_rows']))


def identity(row):
    return (row['shape'], row['counter'], row['fresh_pair'], row['key_count'])


reference = {identity(row): statistics.median(row['samples_seconds']) for row in sources[0][2]}
earlier = {identity(row): statistics.median(row['samples_seconds']) for row in sources[1][2]}
assert len(reference) == len(earlier) == 12
median_rows, sample_rows = [], []
for label, path, rows in sources:
    assert {identity(row) for row in rows} == set(reference)
    for row in rows:
        assert row['updates'] == row['key_count'] * 4
        assert row['checksums'] == [row['updates']] * 3
        assert len(row['samples_seconds']) == 3
        median = statistics.median(row['samples_seconds'])
        common = {'runtime': label, 'shape': row['shape'], 'counter': row['counter'],
                  'fresh_pair': row['fresh_pair'], 'key_count': row['key_count'],
                  'updates': row['updates'], 'diagnostic_only': True, 'source': path.name}
        median_rows.append(dict(common, median_seconds=median,
                                cpython_time_divided_by_runtime_time=reference[identity(row)] / median,
                                runtime_time_divided_by_cpython_time=median / reference[identity(row)],
                                earlier_xlang3_time_divided_by_runtime_time=earlier[identity(row)] / median))
        for index, (seconds, checksum) in enumerate(zip(row['samples_seconds'], row['checksums'])):
            sample_rows.append(dict(common, sample_index=index, seconds=seconds, checksum=checksum))
assert len(median_rows) == 60 and len(sample_rows) == 180
outputs = []
for suffix, rows in (('diagnostic-medians', median_rows), ('diagnostic-samples', sample_rows)):
    path = data / ('dict-intrinsic-index-20261007-' + suffix + '.csv')
    assert not path.exists()
    with path.open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    outputs.append({'path': path.name, 'rows': len(rows), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
proof = data / 'dict-intrinsic-index-20261007-diagnostic-export.json'
assert not proof.exists()
proof.write_text(json.dumps({'scope': 'Diagnostic samples only; no official BPE result or full-suite score is assigned',
                             'shared_probe_sha256': probe_hash, 'outputs': outputs}, indent=2) + '\n', encoding='utf-8')
print('Exported 60 diagnostic medians and 180 checked raw samples; no official score')
