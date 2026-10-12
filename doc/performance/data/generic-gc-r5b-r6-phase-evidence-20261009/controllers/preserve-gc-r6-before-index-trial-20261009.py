"""Preserve terminal R6 source and Release before another engine edit."""
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
app_path = DATA / 'gc-generic-cycles-applied-source-r6-20261009.json'
build_path = DATA / 'gc-generic-cycles-build-r6-20261009.json'
perf_path = DATA / 'gc-generic-cycles-performance-r6-activity-20261009.json'
assert sha(perf_path) == '482c01037f225e636d1f487402fbd8379cfa1e82e631db7589de034ef4d0d9c5'
app, build, perf = map(read, (app_path, build_path, perf_path))
assert perf['terminal'] and perf['hashes_unchanged'] and not perf['fixed_gate']['passed']
assert all(p['measurement_valid'] for p in perf['phases'])
assert all(p['completed'] for p in perf['official'].values())
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
assert all(sha(RELEASE / p) == h for p, h in build['release_sha256'].items())
control = ROOT / 'build-repro/controls/gc-generic-cycles-r6-inconclusive-gate-20261009'
assert not control.exists()
control.mkdir()
shutil.copytree(RELEASE, control / 'Release')
for p, h in app['source_sha256'].items():
    target = control / 'sources' / p
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / p, target)
    assert sha(target) == h
assert all(sha(control / 'Release' / p) == h for p, h in build['release_sha256'].items())
assert all(sha(ROOT / 'build-repro/Release' / p) == h for p, h in app['fixed_baseline_sha256'].items())
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
(control / 'provenance.json').write_text(json.dumps({'engine_commit_permitted': False,
    'application_sha256': sha(app_path), 'build_sha256': sha(build_path), 'performance_sha256': sha(perf_path),
    'source_sha256': app['source_sha256'], 'release_sha256': build['release_sha256'],
    'scope': 'R6 correctness pass; fixed gate2 inconclusive, not accepted baseline.',
    'controller_sha256': sha(__file__)}, indent=2) + '\n', encoding='utf-8', newline='\n')
print('R6 preserved:', sha(control / 'provenance.json'))
