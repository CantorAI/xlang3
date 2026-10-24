"""Preserve terminal R3, then reuse unique graph edges for reachability."""
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
old_app = DATA / 'gc-generic-cycles-applied-source-r3-20261009.json'
perf = DATA / 'gc-generic-cycles-performance-r3-20261009.json'
assert sha(old_app) == '165e474cac11bb2866e5c4534721f55ef15c287e09723f8c6b48c2b10d5fbeb0'
assert sha(perf) == '880969fe66518fae3e8c7b2b0c46467f83f196278b691d32e5685a78a5cb3056'
app, result = read(old_app), read(perf)
assert result['terminal'] and result['hashes_unchanged'] and not result['fixed_gate']['passed']
assert all(sha(p) == h for p, h in result['pins_before'].items())
control = ROOT / 'build-repro/controls/gc-generic-cycles-r3-failed-gate-20261009'
assert not control.exists()
control.mkdir()
shutil.copytree(RELEASE, control / 'Release')
for p, h in app['source_sha256'].items():
    target = control / 'sources' / p
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / p, target)
    assert sha(target) == h
binaries = read(DATA / 'gc-generic-cycles-build-r3-20261009.json')['release_sha256']
assert all(sha(control / 'Release' / p) == h for p, h in binaries.items())
provenance = {'accepted_baseline': False, 'parent_application_sha256': sha(old_app),
    'performance_sha256': sha(perf), 'source_sha256': app['source_sha256'],
    'release_sha256': binaries, 'controller_sha256': sha(__file__)}
(control / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')

path = ROOT / 'src/runtime/modules/system/gc_plain_cycles.h'
source = path.read_text()
old = '    std::vector<std::vector<size_t>> parents(nodes.size());'
new = '''    std::vector<std::vector<size_t>> parents(nodes.size()), children(nodes.size());'''
assert source.count(old) == 1
source = source.replace(old, new)
old = '          parents[target->second].push_back(source); node.last_parent = source;'
new = '''          parents[target->second].push_back(source);
          children[source].push_back(target->second);
          node.last_parent = source;'''
assert source.count(old) == 1
source = source.replace(old, new)
old = '''      gc_visit_plain_owned_runs(nodes[source].object, [&](Object* object, uint64_t) {
        const auto target = index.find(object);
        if (target != index.end() && !nodes[target->second].reachable) {
          nodes[target->second].reachable = true; pending.push_back(target->second);
        }
      });'''
new = '''      // Reachability needs each target once, unlike incoming ownership.
      // Reuse the exact adjacency built above instead of rereading potentially
      // large repeated-reference containers and probing their graph indexes.
      for (size_t target : children[source]) {
        if (!nodes[target].reachable) {
          nodes[target].reachable = true; pending.push_back(target);
        }
      }'''
assert source.count(old) == 1
source = source.replace(old, new)
path.write_text(source, encoding='utf-8', newline='\n')
current = {p: sha(ROOT / p) for p in app['source_sha256']}
assert {p for p in current if current[p] != app['source_sha256'][p]} == {'src/runtime/modules/system/gc_plain_cycles.h'}
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
app.update(status='generic_gc_cached_adjacency_r4_applied_validation_pending', source_sha256=current,
    integration_update={'parent_application_sha256': sha(old_app), 'terminal_failed_gate_sha256': sha(perf),
        'reason': 'Cache unique forward adjacency during owning-edge counting; reachability no longer rescans Value storage. Owning multiplicity and retirement unchanged.',
        'algorithm_changed': True, 'preserved_r3_control': str(control),
        'preserved_r3_control_sha256': sha(control / 'provenance.json')}, controller_sha256=sha(__file__))
out = DATA / 'gc-generic-cycles-applied-source-r4-20261009.json'
assert not out.exists()
out.write_text(json.dumps(app, indent=2) + '\n', encoding='utf-8', newline='\n')
for name in ('build', 'check'):
    prefix = 'build-gc-generic-cycles' if name == 'build' else 'check-gc-generic-cycles-correctness'
    original = ROOT / ('scratch/performance/' + prefix + '-r3-root-20261009.py')
    target = ROOT / ('scratch/performance/' + prefix + '-r4-root-20261009.py')
    text = original.read_text().replace(sha(old_app), sha(out))
    text = text.replace('applied-source-r3-', 'applied-source-r4-').replace('build-r3-', 'build-r4-')
    if name == 'check': text = text.replace('correctness-r3-20261009', 'correctness-r4-20261009')
    assert not target.exists()
    target.write_text(text, encoding='utf-8', newline='\n')
print(json.dumps({'application_sha256': sha(out), 'source_sha256': sha(path)}, indent=2))
