"""One unchanged official body pass; diagnostic evidence, never a suite score."""
import hashlib
import json
from pathlib import Path
import runpy
import sys

script = Path(r'C:\Python\Python314\Lib\site-packages\pyperformance\data-files\benchmarks\bm_bpe_tokeniser\run_benchmark.py')
source_hash = hashlib.sha256(script.read_bytes()).hexdigest()
assert source_hash == 'c7255345499e118181785370b0996e8cc1056491b9678b3fbeb02421e3f9b2df'
namespace = runpy.run_path(str(script), run_name='bpe_body_diagnostic')
seconds = namespace['bench_bpe_tokeniser'](1)
assert seconds > 0
data = script.parent / 'data/frankenstein_intro.txt'
print(json.dumps({'purpose': 'one unchanged official body pass, diagnostic only',
                  'python_version': sys.version, 'benchmark_source_sha256': source_hash,
                  'benchmark_data_sha256': hashlib.sha256(data.read_bytes()).hexdigest(),
                  'loops': 1, 'body_seconds': seconds,
                  'official_roundtrip_assertion': 'passed'}))
