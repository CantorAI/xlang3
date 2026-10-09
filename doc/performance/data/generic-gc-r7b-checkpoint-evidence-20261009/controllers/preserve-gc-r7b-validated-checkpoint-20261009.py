"""Preserve the correctness/gate/original-benchmark validated GC checkpoint."""
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
app_path = DATA / 'gc-generic-cycles-applied-source-r7b-20261009.json'
build_path = DATA / 'gc-generic-cycles-build-r7b-20261009.json'
correct_path = DATA / 'gc-generic-cycles-correctness-r7b-20261009.json'
perf_path = DATA / 'gc-generic-cycles-performance-r7b-activity-20261009.json'
assert sha(app_path) == '27396bda5dae3e95a7fad24b6c27cd617e092353e323a435987ee3262ef4ee23'
assert sha(correct_path) == '897ce79dd68c514e3c1cc591aa481ce3a71ce1ecf82b48b1393291e7ab37d216'
assert sha(perf_path) == '2b0308d6131394ef7f87bed13c722015b22af1d5b30ab43921bafe2560e39874'
app, build, correct, perf = map(read, (app_path, build_path, correct_path, perf_path))
assert correct['terminal'] and correct['correctness_passed'] and correct['sources_unchanged'] and correct['release_unchanged'] and correct['fixed_baseline_unchanged']
assert perf['terminal'] and perf['hashes_unchanged'] and perf['fixed_gate']['passed']
assert perf['status'] == 'gate_passed_original_gc_completed'
assert all(p['measurement_valid'] for p in perf['phases'])
assert all(p['completed'] for p in perf['official'].values())
assert app['source_sha256'] == build['source_sha256'] == correct['source_sha256'] and app['source_count'] == 143
assert len(build['release_sha256']) == 178 and correct['release_sha256'] == build['release_sha256']
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
assert all(sha(RELEASE / p) == h for p, h in build['release_sha256'].items())
assert all(sha(ROOT / 'build-repro/Release' / p) == h for p, h in app['fixed_baseline_sha256'].items())
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
control = ROOT / 'build-repro/controls/gc-generic-cycles-r7b-accepted-20261009'
assert not control.exists()
control.mkdir()
shutil.copytree(RELEASE, control / 'Release')
for p, h in app['source_sha256'].items():
    target = control / 'sources' / p
    target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(ROOT / p, target)
    assert sha(target) == h
assert all(sha(control / 'Release' / p) == h for p, h in build['release_sha256'].items())
record = {'terminal': True, 'status': 'preserved_fully_validated_generic_gc_r7b',
    'full_validated': True, 'correctness_passed': True, 'fixed_gate_passed': True, 'affected_originals_completed': True,
    'application_sha256': sha(app_path), 'build_sha256': sha(build_path), 'correctness_sha256': sha(correct_path),
    'performance_sha256': sha(perf_path), 'source_count': 143, 'file_count': 178,
    'source_sha256': app['source_sha256'], 'release_sha256': build['release_sha256'],
    'fixed_baseline_sha256': app['fixed_baseline_sha256'], 'unowned_tracked_dirty_sha256': app['unowned_tracked_dirty_sha256'],
    'scope': 'Full correctness/default11 gate/affected original GC cases passed. Full97 pending. Selected dirty-worktree source provenance, not clean-checkout proof. Conservative native/finalizer GC boundaries remain.',
    'controller_sha256': sha(__file__)}
out = control / 'provenance.json'
out.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
receipt = DATA / 'gc-generic-cycles-r7b-preserved-validated-checkpoint-20261009.json'
assert not receipt.exists()
receipt.write_text(json.dumps({**record, 'control': str(control), 'control_provenance_sha256': sha(out)}, indent=2) + '\n', encoding='utf-8', newline='\n')
print('Preserved validated checkpoint', sha(receipt))
