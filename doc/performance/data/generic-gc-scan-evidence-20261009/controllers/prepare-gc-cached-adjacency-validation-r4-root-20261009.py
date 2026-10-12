"""Retain the identical fixed gate and original GC protocol for the R4 graph."""
import hashlib
from pathlib import Path

ROOT = Path('D:/CantorAI/xlang3')
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
app = ROOT / 'doc/performance/data/gc-generic-cycles-applied-source-r4-20261009.json'
correct = ROOT / 'doc/performance/data/gc-generic-cycles-correctness-r4-20261009.json'
assert sha(app) == '8b00bc5a7bb84a4440a32c4e789229e6000434829b70c0c0e08adb3496f45362'
assert sha(correct) == '6edf927bae4e535a9f23f309c5e3172f89f6e8167023cd55c29b120d235e660c'
original = ROOT / 'scratch/performance/validate-gc-generic-cycles-performance-r3-root-20261009.py'
target = ROOT / 'scratch/performance/validate-gc-generic-cycles-performance-r4-root-20261009.py'
assert not target.exists()
source = original.read_text().replace('165e474cac11bb2866e5c4534721f55ef15c287e09723f8c6b48c2b10d5fbeb0', sha(app))
source = source.replace('894b32d12e767fe46233ef080abbefdb30e27d1a2932a47ebf459fcba84e8243', sha(correct))
source = source.replace('applied-source-r3-', 'applied-source-r4-')
source = source.replace('gc-generic-cycles-correctness-r3-20261009', 'gc-generic-cycles-correctness-r4-20261009')
source = source.replace("PREFIX = 'gc-generic-cycles-performance-r3-20261009'", "PREFIX = 'gc-generic-cycles-performance-r4-20261009'")
target.write_text(source, encoding='utf-8', newline='\n')
print(sha(target))
