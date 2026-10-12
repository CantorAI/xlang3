"""Pin fresh R3 correctness and retain the identical gate/official protocol."""
import hashlib
from pathlib import Path

ROOT = Path('D:/CantorAI/xlang3')
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
app = ROOT / 'doc/performance/data/gc-generic-cycles-applied-source-r3-20261009.json'
correct = ROOT / 'doc/performance/data/gc-generic-cycles-correctness-r3-20261009.json'
assert sha(app) == '165e474cac11bb2866e5c4534721f55ef15c287e09723f8c6b48c2b10d5fbeb0'
assert sha(correct) == '894b32d12e767fe46233ef080abbefdb30e27d1a2932a47ebf459fcba84e8243'
original = ROOT / 'scratch/performance/validate-gc-generic-cycles-performance-root-20261009.py'
target = ROOT / 'scratch/performance/validate-gc-generic-cycles-performance-r3-root-20261009.py'
assert not target.exists()
source = original.read_text()
source = source.replace('7a4e8ba4fe279497c48e11f5790175ed0197b51b0c262e2a2fdd753c4a974ad0', sha(app))
source = source.replace('4a95b8ad70876f6638d9d98fb98e02715f104190a1439a1b4d6c17e29a258fc2', sha(correct))
source = source.replace('applied-source-r2-', 'applied-source-r3-')
source = source.replace('gc-generic-cycles-correctness-20261009', 'gc-generic-cycles-correctness-r3-20261009')
source = source.replace("PREFIX = 'gc-generic-cycles-performance-20261009'", "PREFIX = 'gc-generic-cycles-performance-r3-20261009'")
target.write_text(source, encoding='utf-8', newline='\n')
print(sha(target))
