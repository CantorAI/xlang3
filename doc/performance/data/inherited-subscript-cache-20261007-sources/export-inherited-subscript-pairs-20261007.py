"""Export completed paired evidence without running a runtime or measurement."""
import csv
import io
import json
from pathlib import Path
import statistics
import sys

assert sys.version_info[:3] == (3, 14, 7)
data = Path.cwd() / 'doc/performance/data'
paired = json.loads((data / 'inherited-subscript-cache-paired-20261007.json').read_text())
assert paired['status'] == 'terminal' and len(paired['pairs']) == 7
samples = io.StringIO(newline='')
writer = csv.writer(samples)
writer.writerow(['pair', 'process_order', 'runtime', 'path', 'key_count', 'updates', 'sample', 'seconds'])
sample_count = 0
for index, pair in enumerate(paired['pairs'], 1):
    assert pair['order'] == (['control', 'candidate'] if index % 2 else ['candidate', 'control'])
    for order, runtime in enumerate(pair['order'], 1):
        for row in pair['runs'][runtime]['rows']:
            assert row['all_values_verified'] and len(row['samples_seconds']) == 5
            for sample, seconds in enumerate(row['samples_seconds'], 1):
                writer.writerow([index, order, runtime, row['path'], row['key_count'], row['updates'], sample, seconds])
                sample_count += 1
assert sample_count == 980
summary = io.StringIO(newline='')
writer = csv.writer(summary)
writer.writerow(['path', 'key_count', 'median_control_over_candidate_speedup', 'favorable_pairs', 'total_pairs'])
table = []
for index, row in enumerate(paired['summary']):
    ratios = []
    for pair in paired['pairs']:
        before = pair['runs']['control']['rows'][index]
        after = pair['runs']['candidate']['rows'][index]
        assert (before['path'], before['key_count']) == (after['path'], after['key_count']) == (row['path'], row['key_count'])
        ratios.append(statistics.median(before['samples_seconds']) / statistics.median(after['samples_seconds']))
    assert ratios == row['control_time_divided_by_candidate_time']
    assert statistics.median(ratios) == row['median_speedup']
    favorable = sum(ratio > 1 for ratio in ratios)
    assert favorable == row['pairs_favoring_candidate']
    writer.writerow([row['path'], row['key_count'], row['median_speedup'], favorable, 7])
    table.append(f"| {row['path']} | {row['key_count']} | {row['median_speedup']:.3f}× | {favorable}/7 |")
assert len(table) == 14
sample_path = data / 'inherited-subscript-cache-paired-samples-20261007.csv'
summary_path = data / 'inherited-subscript-cache-paired-summary-20261007.csv'
for target, content in ((sample_path, samples), (summary_path, summary)):
    raw = content.getvalue().encode('utf-8')
    if target.exists():
        assert target.read_bytes() == raw, target
    else:
        target.write_bytes(raw)
print('Exported/verified', sample_count, 'raw samples and', len(table), 'summary rows')
