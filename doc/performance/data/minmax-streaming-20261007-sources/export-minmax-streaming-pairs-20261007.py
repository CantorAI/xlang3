"""Export terminal diagnostics without running an interpreter workload."""
import csv
import io
import json
from pathlib import Path
import statistics
import sys

assert sys.version_info[:3] == (3, 14, 7)
data = Path.cwd() / 'doc/performance/data'
paired = json.loads((data / 'minmax-streaming-paired-callbacks-20261007.json').read_text())
assert paired['status'] == 'terminal' and len(paired['pairs']) == 7
raw_samples = io.StringIO(newline='')
writer = csv.writer(raw_samples)
writer.writerow(['pair', 'process_order', 'runtime', 'mapping', 'path', 'lookups', 'sample', 'seconds', 'checksum'])
sample_count = 0
for number, pair in enumerate(paired['pairs'], 1):
    assert pair['order'] == (['control', 'candidate'] if number % 2 else ['candidate', 'control'])
    for order, runtime in enumerate(pair['order'], 1):
        for row in pair['runs'][runtime]['rows']:
            assert row['checksum'] == 8 * 1023 and row['lookups_per_sample'] == 8192
            assert len(row['samples_seconds']) == 5
            for sample, seconds in enumerate(row['samples_seconds'], 1):
                writer.writerow([number, order, runtime, row['mapping'], row['path'], 8192, sample, seconds, row['checksum']])
                sample_count += 1
assert sample_count == 420
raw_summary = io.StringIO(newline='')
writer = csv.writer(raw_summary)
writer.writerow(['mapping', 'path', 'median_control_over_candidate_speedup', 'favorable_pairs', 'total_pairs'])
table = []
for index, row in enumerate(paired['summary']):
    ratios = []
    for pair in paired['pairs']:
        before, after = (pair['runs'][name]['rows'][index] for name in ('control', 'candidate'))
        assert (before['mapping'], before['path']) == (after['mapping'], after['path']) == (row['mapping'], row['path'])
        ratios.append(statistics.median(before['samples_seconds']) / statistics.median(after['samples_seconds']))
    assert ratios == row['control_time_over_candidate_time']
    assert statistics.median(ratios) == row['median_speedup']
    favorable = sum(value > 1 for value in ratios)
    assert favorable == row['pairs_favoring_candidate']
    writer.writerow([row['mapping'], row['path'], row['median_speedup'], favorable, 7])
    table.append(f"| {row['mapping']} | {row['path']} | {row['median_speedup']:.3f}× | {favorable}/7 |")
assert len(table) == 6
sample_path = data / 'minmax-streaming-paired-samples-20261007.csv'
summary_path = data / 'minmax-streaming-paired-summary-20261007.csv'
for path, content in ((sample_path, raw_samples), (summary_path, raw_summary)):
    raw = content.getvalue().encode('utf-8')
    if path.exists():
        assert path.read_bytes() == raw
    else:
        path.write_bytes(raw)
print('Exported/verified', sample_count, 'samples and', len(table), 'summary rows')
