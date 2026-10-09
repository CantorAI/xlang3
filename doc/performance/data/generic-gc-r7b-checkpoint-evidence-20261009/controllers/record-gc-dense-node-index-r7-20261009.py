"""Record the bounded node-index trial after preserving terminal R6."""
import hashlib
import json
from pathlib import Path

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
old = DATA / 'gc-generic-cycles-applied-source-r6-20261009.json'
assert sha(old) == '22d1313f3f244ba0a4c8a488c5f8a01bcecbb2758609405bfda5d02baa44f71e'
app = read(old)
build = read(DATA / 'gc-generic-cycles-build-r6-20261009.json')
control = ROOT / 'build-repro/controls/gc-generic-cycles-r6-inconclusive-gate-20261009'
assert all(sha(control / 'sources' / p) == h for p, h in app['source_sha256'].items())
assert all(sha(control / 'Release' / p) == h for p, h in build['release_sha256'].items())
assert all(sha(ROOT / 'build-repro/main-verify-20261006/Release' / p) == h for p, h in build['release_sha256'].items())
current = {p: sha(ROOT / p) for p in app['source_sha256']}
assert {p for p in current if current[p] != app['source_sha256'][p]} == {
    'src/runtime/modules/system/gc_plain_cycles.h', 'tests/cpp/generic_gc_cases.h'}
new_header = 'src/runtime/modules/system/gc_plain_node_index.h'
assert new_header not in current
current[new_header] = sha(ROOT / new_header)
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
assert all(sha(ROOT / 'build-repro/Release' / p) == h for p, h in app['fixed_baseline_sha256'].items())
app.update(status='generic_gc_bounded_node_index_r7_applied_validation_pending',
    source_sha256=current, source_count=len(current), owned_paths=app['owned_paths'] + [new_header],
    integration_update={'parent_application_sha256': sha(old), 'known_r6_control': str(control),
        'r6_performance_sha256': sha(DATA / 'gc-generic-cycles-performance-r6-activity-20261009.json'),
        'reason': 'R6 GC gate remains inconclusive at 1.117x; R5b node construction80.3us. Replace per-node hash allocation with bounded registry-index table; pointer identity and lazy exact hash fallback preserve membership during compaction. No ownership or object header changes.',
        'prior_mechanism_search': 'No matching gc dense-index trial in doc/performance gc reports/patches; read existing weakref indexes, but do not attribute specialized phase to their membership scans.'},
    controller_sha256=sha(__file__))
out = DATA / 'gc-generic-cycles-applied-source-r7-20261009.json'
assert not out.exists()
out.write_text(json.dumps(app, indent=2) + '\n', encoding='utf-8', newline='\n')
for prefix in ('build-gc-generic-cycles', 'check-gc-generic-cycles-correctness'):
    original = ROOT / ('scratch/performance/' + prefix + '-r6-root-20261009.py')
    target = ROOT / ('scratch/performance/' + prefix + '-r7-root-20261009.py')
    source = original.read_text().replace(sha(old), sha(out)).replace('-r6-', '-r7-')
    assert not target.exists()
    target.write_text(source, encoding='utf-8', newline='\n')
print(json.dumps({'application_sha256': sha(out), 'source_count': len(current),
    'header_sha256': sha(ROOT / new_header)}, indent=2))
