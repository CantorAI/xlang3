"""Export terminal checked pair samples and preserve every control row."""
import csv
import io
import json
from pathlib import Path
import statistics
import sys

assert sys.version_info[:3] == (3, 14, 7)
data = Path.cwd() / 'doc/performance/data'
paired = json.loads((data / 'native-trivial-callback-paired-20261007.json').read_text())
assert paired['status'] == 'terminal' and len(paired['probes']) == 2
samples = io.StringIO(newline='')
summary = io.StringIO(newline='')
sample_writer, summary_writer = csv.writer(samples), csv.writer(summary)
sample_writer.writerow(['probe', 'pair', 'process_order', 'runtime', 'mapping', 'path', 'operations', 'sample', 'seconds', 'checked_result'])
summary_writer.writerow(['probe', 'mapping', 'path', 'median_control_over_candidate_speedup', 'favorable_pairs', 'total_pairs'])
sample_count = 0
table = []
for observed in paired['probes']:
    assert len(observed['pairs']) == 7
    for number, pair in enumerate(observed['pairs'], 1):
        assert pair['order'] == (['control', 'candidate'] if number % 2 else ['candidate', 'control'])
        for order, runtime in enumerate(pair['order'], 1):
            for row in pair['runs'][runtime]['rows']:
                assert len(row['samples_seconds']) == 5
                operations = row.get('operations_per_sample', row.get('lookups_per_sample'))
                assert operations == 8192
                checked = row.get('checked_result', row.get('checksum'))
                assert checked == ((32 if row['path'] == 'native_max_constant_key' else 0) if observed['name'] == 'trivial' else 8 * 1023)
                for sample, seconds in enumerate(row['samples_seconds'], 1):
                    sample_writer.writerow([observed['name'], number, order, runtime, row.get('mapping', ''), row['path'], operations, sample, seconds, checked])
                    sample_count += 1
    for index, row in enumerate(observed['summary']):
        ratios = []
        for pair in observed['pairs']:
            before, after = (pair['runs'][name]['rows'][index] for name in ('control', 'candidate'))
            assert (before.get('mapping'), before['path']) == (after.get('mapping'), after['path']) == (row['mapping'], row['path'])
            ratios.append(statistics.median(before['samples_seconds']) / statistics.median(after['samples_seconds']))
        assert ratios == row['control_time_over_candidate_time']
        assert statistics.median(ratios) == row['median_speedup']
        favorable = sum(ratio > 1 for ratio in ratios)
        assert favorable == row['pairs_favoring_candidate']
        summary_writer.writerow([observed['name'], row['mapping'] or '', row['path'], row['median_speedup'], favorable, 7])
        table.append(f"| {observed['name']} | {row['mapping'] or '—'} | {row['path']} | {row['median_speedup']:.3f}× | {favorable}/7 |")
assert sample_count == 770 and len(table) == 11
sample_path = data / 'native-trivial-callback-paired-samples-20261007.csv'
summary_path = data / 'native-trivial-callback-paired-summary-20261007.csv'
for path, content in ((sample_path, samples), (summary_path, summary)):
    raw = content.getvalue().encode('utf-8')
    if path.exists():
        assert path.read_bytes() == raw
    else:
        path.write_bytes(raw)
print('Exported/verified', sample_count, 'raw samples and', len(table), 'summary rows')
