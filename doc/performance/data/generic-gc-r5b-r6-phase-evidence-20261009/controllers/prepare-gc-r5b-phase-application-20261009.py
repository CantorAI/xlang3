"""Reuse phase attribution on the changed R5b engine, preserving prior immutable evidence."""
import hashlib
import json
from pathlib import Path

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
app = json.loads((DATA / 'gc-generic-cycles-applied-source-r5b-20261009.json').read_bytes())
build = json.loads((DATA / 'gc-generic-cycles-build-r5b-20261009.json').read_bytes())
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
assert all(sha(ROOT / 'build-repro/main-verify-20261006/Release' / p) == h for p, h in build['release_sha256'].items())
state = DATA / 'gc-generic-cycles-r5b-before-phase-20261009.json'
assert not state.exists()
state.write_text(json.dumps({'restored_source_sha256': app['source_sha256'],
    'restored_release_sha256': build['release_sha256'], 'controller_sha256': sha(__file__)}, indent=2) + '\n', encoding='utf-8')
original = ROOT / 'scratch/performance/apply-gc-phase-diagnostic-20261009.py'
source = original.read_text()
replacements = {
 'gc-generic-cycles-applied-source-r4-20261009': 'gc-generic-cycles-applied-source-r5b-20261009',
 'gc-generic-cycles-performance-r4-activity-20261009': 'gc-generic-cycles-performance-r5b-20261009',
 '8b00bc5a7bb84a4440a32c4e789229e6000434829b70c0c0e08adb3496f45362': '511976e5a6198bcab69d84361dd274e591a5c833949549d3f229b1a929be7454',
 '6e0f108f4381e20364a300534e6c960caf8147695c6ad7ded3d6c620f2e7f6da': '6b42af7e1e170517618254f16a1c721097638f70a24df5d75f39c66f2a344f14',
 'gc-generic-cycles-r4-failed-gate-20261009': 'gc-generic-cycles-r5b-failed-gate-20261009',
 'gc-generic-cycles-build-r4-20261009': 'gc-generic-cycles-build-r5b-20261009',
 'gc-generic-cycles-phase-diagnostic-applied-20261009': 'gc-generic-cycles-r5b-phase-diagnostic-applied-20261009',
 'gc-generic-cycles-phase-diagnostic-build-20261009': 'gc-generic-cycles-r5b-phase-diagnostic-build-20261009',
 'build-gc-generic-cycles-r4-root-20261009': 'build-gc-generic-cycles-r5b-root-20261009',
 'build-gc-phase-diagnostic-20261009': 'build-gc-r5b-phase-diagnostic-20261009',
 'r4_exact_control': 'r5b_exact_control'
}
for old, new in replacements.items():
    assert old in source, old
    source = source.replace(old, new)
target = ROOT / 'scratch/performance/apply-gc-r5b-phase-diagnostic-20261009.py'
assert not target.exists()
target.write_text(source, encoding='utf-8', newline='\n')
print(sha(target))
