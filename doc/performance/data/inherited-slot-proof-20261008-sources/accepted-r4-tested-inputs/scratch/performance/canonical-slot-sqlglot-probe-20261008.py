"""Time unchanged original SQLGlot parse body with imports outside timing."""
import hashlib
import json
import sys

source_dir = r'C:\Python\Python314\Lib\site-packages\pyperformance\data-files\benchmarks\bm_sqlglot_v2'
sys.path.insert(0, source_dir)
import run_benchmark as original

canonical = original.parse_one(original.SQL).sql()
checksum = hashlib.sha256(canonical.encode('utf-8')).hexdigest()
samples = []
assert original.bench_parse(3) > 0
for unused in range(5):
    elapsed = original.bench_parse(10)
    samples.append(elapsed / 10)
    assert hashlib.sha256(original.parse_one(original.SQL).sql().encode('utf-8')).hexdigest() == checksum
print(json.dumps({'purpose': 'unchanged original parse body, checked AST output, not official score',
                  'source': original.__file__, 'sql_checksum_sha256': checksum,
                  'rows': [{'path': 'sqlglot_v2_parse', 'parses_per_sample': 10,
                            'samples_seconds': samples, 'canonical_sql_sha256': checksum}]}))
