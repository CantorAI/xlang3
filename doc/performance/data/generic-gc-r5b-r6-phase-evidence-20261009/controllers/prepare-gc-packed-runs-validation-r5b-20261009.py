"""Prepare unchanged gate/official cases; wait in-process to avoid a second Python manager."""
import hashlib
from pathlib import Path

ROOT = Path('D:/CantorAI/xlang3')
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
app = ROOT / 'doc/performance/data/gc-generic-cycles-applied-source-r5b-20261009.json'
correct = ROOT / 'doc/performance/data/gc-generic-cycles-correctness-r5b-20261009.json'
assert sha(app) == '511976e5a6198bcab69d84361dd274e591a5c833949549d3f229b1a929be7454'
assert sha(correct) == '8547c7c69ba933bf68841f930b661a6f31d257c55bea09dcaf688be3975a2d93'
original = ROOT / 'scratch/performance/validate-gc-generic-cycles-performance-r4-activity-root-20261009.py'
watch = ROOT / 'scratch/performance/gc-phase-dormant-msbuild-activity-watch-20261009.py'
target = ROOT / 'scratch/performance/validate-gc-generic-cycles-performance-r5b-root-20261009.py'
assert not target.exists()
source = original.read_text().replace('8b00bc5a7bb84a4440a32c4e789229e6000434829b70c0c0e08adb3496f45362', sha(app))
source = source.replace('6edf927bae4e535a9f23f309c5e3172f89f6e8167023cd55c29b120d235e660c', sha(correct))
source = source.replace('applied-source-r4-', 'applied-source-r5b-').replace('correctness-r4-20261009', 'correctness-r5b-20261009')
source = source.replace('gc-dormant-msbuild-activity-watch-20261009.py', watch.name)
source = source.replace('016539435a7182d77232cd9129bba474deb42f0fecfd718c505779b721b28f86', sha(watch))
source = source.replace('watcher.admit_dormant_worker()', 'watcher.admit_dormant_worker(34420)')
source = source.replace("PREFIX = 'gc-generic-cycles-performance-r4-activity-20261009'", "PREFIX = 'gc-generic-cycles-performance-r5b-20261009'")
target.write_text(source, encoding='utf-8', newline='\n')
print(sha(target))
