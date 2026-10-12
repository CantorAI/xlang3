"""Prepare an unscored IR inspection of the original callback; no runtime run."""
import hashlib
import json
from pathlib import Path

source = Path(r'C:\Python\Python314\Lib\site-packages\pyperformance\data-files\benchmarks\bm_bpe_tokeniser\run_benchmark.py')
raw = source.read_bytes()
sha = hashlib.sha256(raw).hexdigest()
assert sha == 'c7255345499e118181785370b0996e8cc1056491b9678b3fbeb02421e3f9b2df'
guard = b'if __name__ == "__main__":'
assert raw.count(guard) == 1
derived = raw.replace(guard, b'if False:')
begin, end = raw.index(b'def bpe_train('), raw.index(b'\ndef train(')
assert derived[begin:end] == raw[begin:end]
scratch = Path.cwd() / 'scratch/performance'
target = scratch / 'bpe-callback-ir-source-20261007.py'
metadata = scratch / 'bpe-callback-ir-source-20261007.json'
assert not target.exists() and not metadata.exists()
target.write_bytes(derived)
metadata.write_text(json.dumps({
    'scope': 'Unscored IR diagnosis only; no timed workload change',
    'original_source': str(source), 'original_sha256': sha,
    'derived_sha256': hashlib.sha256(derived).hexdigest(),
    'trainer_source_sha256': hashlib.sha256(raw[begin:end]).hexdigest(),
    'sole_change': 'Disable the __main__ runner with if False; all training and callback source bytes unchanged',
    'runtime_execution': 'pending until immutable-key hash validation session 37798 is terminal',
}, indent=2) + '\n', encoding='utf-8')
print('Prepared original callback source for later IR-only execution; not run')
