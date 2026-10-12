"""Preserve terminal R2 and group repeated owning graph edges without dropping counts."""
import hashlib
import json
from pathlib import Path
import shutil
import sys

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())

assert sys.version_info[:3] == (3, 14, 7)
old_app = DATA / 'gc-generic-cycles-applied-source-r2-20261009.json'
perf = DATA / 'gc-generic-cycles-performance-20261009.json'
assert sha(old_app) == '7a4e8ba4fe279497c48e11f5790175ed0197b51b0c262e2a2fdd753c4a974ad0'
assert sha(perf) == 'cc9d8077da0a71f31dd3c4df169c4b63e2223017d78f1f29ba60cb1eefb9ab4e'
app, result = read(old_app), read(perf)
assert result['terminal'] and result['hashes_unchanged']
assert not result['fixed_gate']['passed']
assert all(sha(p) == h for p, h in result['pins_before'].items())
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
control = ROOT / 'build-repro/controls/gc-generic-cycles-r2-failed-gate-20261009'
assert not control.exists()
control.mkdir()
shutil.copytree(RELEASE, control / 'Release')
for p, h in app['source_sha256'].items():
    target = control / 'sources' / p
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / p, target)
    assert sha(target) == h
binary_map = read(DATA / 'gc-generic-cycles-build-r2-20261009.json')['release_sha256']
assert all(sha(control / 'Release' / p) == h for p, h in binary_map.items())
provenance = {'status': 'preserved_correctness_passed_failed_performance_control',
    'accepted_baseline': False, 'parent_application_sha256': sha(old_app),
    'performance_sha256': sha(perf), 'source_sha256': app['source_sha256'],
    'release_sha256': binary_map, 'controller_sha256': sha(__file__)}
(control / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')

path = ROOT / 'src/runtime/modules/system/gc_plain_cycles.h'
source = path.read_text(encoding='utf-8')
anchor = 'void gc_publish_empty_plain_storage(Object* object) {'
helper = '''// Repeated owning references still contribute their full multiplicity to
// trial deletion. Group consecutive equal targets only to avoid repeating the
// graph-index hash lookup and reachability work for every list/tuple item.
// Borrowed Values contribute no ownership; the clear phase visits every Value.
template <class Visit>
void gc_visit_plain_owned_runs(Object* object, Visit&& visit) {
  Object* target = nullptr;
  uint64_t multiplicity = 0;
  gc_visit_plain_storage(object, [&](const Value& value) {
    if (value.tag != ValueTag::Object || value.as.obj == nullptr ||
        (value.flags & kXlangValueBorrowedRefFlag) != 0) return;
    if (target == value.as.obj) {
      ++multiplicity;
    } else {
      if (target != nullptr) visit(target, multiplicity);
      target = value.as.obj;
      multiplicity = 1;
    }
  });
  if (target != nullptr) visit(target, multiplicity);
}

'''
assert source.count(anchor) == 1
source = source.replace(anchor, helper + anchor)
old = '''      gc_visit_plain_storage(nodes[source].object, [&](const Value& value) {
        if (value.tag != ValueTag::Object || value.as.obj == nullptr ||
            (value.flags & kXlangValueBorrowedRefFlag) != 0) return;
        const auto target = index.find(value.as.obj);'''
assert source.count(old) == 2
source = source.replace(old, '''      gc_visit_plain_owned_runs(nodes[source].object, [&](Object* object, uint64_t multiplicity) {
        const auto target = index.find(object);''', 1)
source = source.replace('gc_plain_atomic_leaf(value.as.obj)', 'gc_plain_atomic_leaf(object)')
source = source.replace('++node.incoming; // Multiplicity matters, even for repeated list items.',
                        'node.incoming += multiplicity; // Count every owning edge, not just distinct targets.')
source = source.replace(old, '''      gc_visit_plain_owned_runs(nodes[source].object, [&](Object* object, uint64_t) {
        const auto target = index.find(object);''', 1)
path.write_text(source, encoding='utf-8', newline='\n')
current = {p: sha(ROOT / p) for p in app['source_sha256']}
assert {p for p in current if current[p] != app['source_sha256'][p]} == {'src/runtime/modules/system/gc_plain_cycles.h'}
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
app.update(status='generic_gc_owned_edge_runs_r3_applied_validation_pending', source_sha256=current,
    integration_update={'parent_application_sha256': sha(old_app), 'terminal_failed_gate_sha256': sha(perf),
        'reason': 'Group owning graph edges by consecutive equal targets while retaining full incoming multiplicity; leave retirement and opaque boundaries unchanged.',
        'algorithm_changed': True, 'preserved_r2_control': str(control),
        'preserved_r2_control_sha256': sha(control / 'provenance.json')}, controller_sha256=sha(__file__))
out = DATA / 'gc-generic-cycles-applied-source-r3-20261009.json'
assert not out.exists()
out.write_text(json.dumps(app, indent=2) + '\n', encoding='utf-8', newline='\n')
app_hash = sha(out)
for name in ('build', 'check'):
    if name == 'build':
        original = ROOT / 'scratch/performance/build-gc-generic-cycles-r2-root-20261009.py'
        target = ROOT / 'scratch/performance/build-gc-generic-cycles-r3-root-20261009.py'
    else:
        original = ROOT / 'scratch/performance/check-gc-generic-cycles-correctness-root-20261009.py'
        target = ROOT / 'scratch/performance/check-gc-generic-cycles-correctness-r3-root-20261009.py'
    text = original.read_text().replace('7a4e8ba4fe279497c48e11f5790175ed0197b51b0c262e2a2fdd753c4a974ad0', app_hash)
    text = text.replace('applied-source-r2-', 'applied-source-r3-').replace('build-r2-', 'build-r3-')
    if name == 'check': text = text.replace("PREFIX = 'gc-generic-cycles-correctness-20261009'", "PREFIX = 'gc-generic-cycles-correctness-r3-20261009'")
    assert not target.exists()
    target.write_text(text, encoding='utf-8', newline='\n')
print(json.dumps({'application_sha256': app_hash, 'r2_control': str(control),
                  'modified_source_sha256': sha(path)}, indent=2))
