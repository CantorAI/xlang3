"""Preserve the ready diagnostic and restore exact R4, without leaving tracing enabled."""
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
app_path = DATA / 'gc-generic-cycles-phase-diagnostic-applied-20261009.json'
build_path = DATA / 'gc-generic-cycles-phase-diagnostic-build-20261009.json'
app, build = read(app_path), read(build_path)
assert build['passed'] and build['sources_unchanged']
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
assert all(sha(RELEASE / p) == h for p, h in build['release_sha256'].items())
for p in ('gc-generic-cycles-phase-diagnostic-20261009.json', 'gc-generic-cycles-phase-diagnostic-idle-20261009.json'):
    refusal = read(DATA / p)
    assert refusal['terminal'] and refusal['status'] == 'diagnostic_failed' and not refusal['phases']
control = ROOT / 'build-repro/controls/gc-phase-diagnostic-ready-20261009'
assert not control.exists()
control.mkdir()
shutil.copytree(RELEASE, control / 'Release')
for p, h in app['source_sha256'].items():
    target = control / 'sources' / p
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / p, target)
    assert sha(target) == h
assert all(sha(control / 'Release' / p) == h for p, h in build['release_sha256'].items())
(control / 'provenance.json').write_text(json.dumps({'diagnostic_only': True,
    'engine_commit_permitted': False, 'application_sha256': sha(app_path), 'build_sha256': sha(build_path),
    'source_sha256': app['source_sha256'], 'release_sha256': build['release_sha256'],
    'controller_sha256': sha(__file__)}, indent=2) + '\n', encoding='utf-8')
r4 = read(DATA / 'gc-generic-cycles-applied-source-r4-20261009.json')
r4build = read(DATA / 'gc-generic-cycles-build-r4-20261009.json')
prior = ROOT / 'build-repro/controls/gc-generic-cycles-r4-failed-gate-20261009'
changed = [p for p, h in r4['source_sha256'].items() if sha(ROOT / p) != h]
assert set(changed) == {'src/runtime/modules/system/gc_plain_cycles.h', 'src/runtime/modules/system/gc_module.cpp'}
assert all(sha(prior / 'sources' / p) == h for p, h in r4['source_sha256'].items())
assert all(sha(prior / 'Release' / p) == h for p, h in r4build['release_sha256'].items())
for p in changed: shutil.copyfile(prior / 'sources' / p, ROOT / p)
assert set(build['release_sha256']) == set(r4build['release_sha256'])
for p in r4build['release_sha256']: shutil.copyfile(prior / 'Release' / p, RELEASE / p)
assert all(sha(ROOT / p) == h for p, h in r4['source_sha256'].items())
assert all(sha(RELEASE / p) == h for p, h in r4build['release_sha256'].items())
assert all(sha(ROOT / 'build-repro/Release' / p) == h for p, h in r4['fixed_baseline_sha256'].items())
assert all(sha(ROOT / p) == h for p, h in r4['unowned_tracked_dirty_sha256'].items())
out = DATA / 'gc-generic-cycles-r4-restored-after-diagnostic-refusals-20261009.json'
assert not out.exists()
out.write_text(json.dumps({'terminal': True, 'status': 'exact_r4_restored_diagnostic_ready_pending_idle_host',
    'restored_source_sha256': r4['source_sha256'], 'restored_release_sha256': r4build['release_sha256'],
    'diagnostic_control': str(control), 'diagnostic_control_sha256': sha(control / 'provenance.json'),
    'temporary_instrumentation_active': False, 'engine_commit_permitted': False,
    'scope': 'Exact byte restoration at unchanged run path. R4 remains a correctness-passing performance-failing candidate. Diagnostic measurements were refused before launching children.',
    'controller_sha256': sha(__file__)}, indent=2) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({'status': 'exact_r4_restored', 'restoration_sha256': sha(out)}, indent=2))
