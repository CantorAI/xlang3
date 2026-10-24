"""Prepare the owned-file staging verifier, without touching Git's index."""
from pathlib import Path
import ast

root = Path.cwd()
source = root / 'scratch/performance/stage-minmax-streaming-checkpoint-20261007.py'
target = root / 'scratch/performance/stage-native-trivial-callback-checkpoint-20261007.py'
assert not target.exists()
text = source.read_text().replace('minmax-streaming', 'native-trivial-callback')
text = text.replace("inventory['raw_sample_count'] == 420", "inventory['raw_sample_count'] == 770")
ast.parse(text)
target.write_bytes(text.encode('utf-8'))
print('Prepared staging verifier; no files staged before benchmark termination')
