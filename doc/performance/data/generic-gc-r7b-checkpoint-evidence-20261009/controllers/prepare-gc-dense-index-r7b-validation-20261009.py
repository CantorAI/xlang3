"""Prepare unchanged gate and original GC cases for correctness-passing R7b."""
import hashlib
from pathlib import Path

ROOT = Path('D:/CantorAI/xlang3')
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
app = ROOT / 'doc/performance/data/gc-generic-cycles-applied-source-r7b-20261009.json'
correct = ROOT / 'doc/performance/data/gc-generic-cycles-correctness-r7b-20261009.json'
assert sha(app) == '27396bda5dae3e95a7fad24b6c27cd617e092353e323a435987ee3262ef4ee23'
assert sha(correct) == '897ce79dd68c514e3c1cc591aa481ce3a71ce1ecf82b48b1393291e7ab37d216'
original = ROOT / 'scratch/performance/validate-gc-generic-cycles-performance-r6-activity-root-20261009.py'
assert sha(original) == '2446b31321c189f8570c365e35936535ac2a7adf50ac35b88fdb3d75ee34c710'
manager = ROOT / 'scratch/performance/validate-gc-generic-cycles-performance-r7b-activity-root-20261009.py'
assert not manager.exists()
source = original.read_text().replace('22d1313f3f244ba0a4c8a488c5f8a01bcecbb2758609405bfda5d02baa44f71e', sha(app))
source = source.replace('ec7cfea6bd351776eb4b5c7c1d43415c3b73482fc54f69ad34ff445c3e0a9b5e', sha(correct))
manager.write_text(source.replace('-r6-', '-r7b-'), encoding='utf-8', newline='\n')
waiter = ROOT / 'scratch/performance/wait-gc-dense-index-r7b-activity-validation-20261009.py'
assert not waiter.exists()
source = (ROOT / 'scratch/performance/wait-gc-flat-adjacency-r6-activity-validation-20261009.py').read_text()
waiter.write_text(source.replace('-r6-', '-r7b-'), encoding='utf-8', newline='\n')
print('manager', sha(manager))
print('waiter', sha(waiter))
