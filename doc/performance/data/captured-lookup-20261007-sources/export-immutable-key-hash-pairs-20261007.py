"""Export every terminal diagnostic row and raw sample with checked ratios."""
import csv
import io
import json
from pathlib import Path
import statistics

data = Path.cwd() / 'doc/performance/data'
record = json.loads((data / 'immutable-key-hash-paired-20261007.json').read_text(encoding='utf-8'))
assert record['status'] == 'terminal'
samples, summary = io.StringIO(newline=''), io.StringIO(newline='')
sw, rw = csv.writer(samples), csv.writer(summary)
sw.writerow(['probe', 'case', 'pair', 'process_order', 'runtime', 'sample', 'seconds', 'checked_result'])
rw.writerow(['probe', 'case', 'median_control_over_candidate_speedup', 'favorable_pairs', 'total_pairs'])
count, table = 0, []

def identity(row):
    return {key: value for key, value in row.items() if key not in ('samples_seconds', 'checksums', 'checked_hash_calls')}

def label(row):
    if 'bytes_width' in row:
        return f"{row['shape']} {row['bytes_width']} B, fresh={row['fresh_tuple']}, hashes={row['hashes_per_key']}"
    if 'key_count' in row:
        return f"{row['shape']} {'Counter' if row['counter'] else 'dict'}, keys={row['key_count']}, fresh={row['fresh_pair']}"
    return (row.get('mapping', '') or '').strip() + ' ' + row['path']

for probe in record['probes']:
    assert len(probe['pairs']) == 7
    for number, pair in enumerate(probe['pairs'], 1):
        assert pair['order'] == (['control', 'candidate'] if number % 2 else ['candidate', 'control'])
        for order, runtime in enumerate(pair['order'], 1):
            for row in pair['runs'][runtime]['rows']:
                checks = row.get('checked_hash_calls', row.get('checksums'))
                if checks is None:
                    checks = [row.get('checked_result', row.get('checksum'))] * len(row['samples_seconds'])
                assert len(checks) == len(row['samples_seconds'])
                for index, seconds in enumerate(row['samples_seconds']):
                    sw.writerow([probe['name'], label(row).strip(), number, order, runtime, index + 1, seconds, checks[index]])
                    count += 1
    for index, row in enumerate(probe['summary']):
        ratios = []
        for pair in probe['pairs']:
            before, after = (pair['runs'][name]['rows'][index] for name in ('control', 'candidate'))
            assert identity(before) == identity(after) == row['identity']
            ratios.append(statistics.median(before['samples_seconds']) / statistics.median(after['samples_seconds']))
        assert ratios == row['control_time_over_candidate_time']
        assert statistics.median(ratios) == row['median_speedup']
        assert sum(value > 1 for value in ratios) == row['pairs_favoring_candidate']
        case = label(row['identity']).strip()
        rw.writerow([probe['name'], case, row['median_speedup'], row['pairs_favoring_candidate'], 7])
        table.append(f"| {probe['name']} | {case} | {row['median_speedup']:.3f}× | {row['pairs_favoring_candidate']}/7 |")
assert count == 1834 and len(table) == 31
sample_path = data / 'immutable-key-hash-paired-samples-20261007.csv'
summary_path = data / 'immutable-key-hash-paired-summary-20261007.csv'
for path, content in ((sample_path, samples), (summary_path, summary)):
    raw = content.getvalue().encode('utf-8')
    if path.exists():
        assert path.read_bytes() == raw
    else:
        path.write_bytes(raw)
print('Exported and checked', count, 'raw samples and', len(table), 'summary rows')
