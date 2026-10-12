"""Pin R6 validation to fresh correctness; retain the original gate and cases."""
import hashlib
from pathlib import Path

ROOT = Path('D:/CantorAI/xlang3')
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
app = ROOT / 'doc/performance/data/gc-generic-cycles-applied-source-r6-20261009.json'
correct = ROOT / 'doc/performance/data/gc-generic-cycles-correctness-r6-20261009.json'
assert sha(app) == '22d1313f3f244ba0a4c8a488c5f8a01bcecbb2758609405bfda5d02baa44f71e'
assert sha(correct) == 'ec7cfea6bd351776eb4b5c7c1d43415c3b73482fc54f69ad34ff445c3e0a9b5e'
original = ROOT / 'scratch/performance/validate-gc-generic-cycles-performance-r5b-root-20261009.py'
assert sha(original) == '9cd42b7d46608204512cfbdd696e04d32644c28fdfb01a8409fd4967bf89a506'
target = ROOT / 'scratch/performance/validate-gc-generic-cycles-performance-r6-root-20261009.py'
assert not target.exists()
source = original.read_text().replace('511976e5a6198bcab69d84361dd274e591a5c833949549d3f229b1a929be7454', sha(app))
source = source.replace('8547c7c69ba933bf68841f930b661a6f31d257c55bea09dcaf688be3975a2d93', sha(correct))
source = source.replace('-r5b-', '-r6-')
target.write_text(source, encoding='utf-8', newline='\n')
waiter = ROOT / 'scratch/performance/wait-gc-flat-adjacency-validation-r6-20261009.py'
assert not waiter.exists()
source = (ROOT / 'scratch/performance/wait-gc-packed-runs-validation-r5b-20261009.py').read_text()
source = source.replace('-r5b-', '-r6-').replace('r5b_readiness_watch', 'r6_readiness_watch')
waiter.write_text(source, encoding='utf-8', newline='\n')
print('manager', sha(target))
print('waiter', sha(waiter))
