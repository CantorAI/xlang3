"""Remove per-node adjacency allocations after measured R5b edge/teardown cost."""
import hashlib
import json
from pathlib import Path

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
old = DATA / 'gc-generic-cycles-applied-source-r5b-20261009.json'
assert sha(old) == '511976e5a6198bcab69d84361dd274e591a5c833949549d3f229b1a929be7454'
app = read(old)
build = read(DATA / 'gc-generic-cycles-build-r5b-20261009.json')
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
assert all(sha(ROOT / 'build-repro/main-verify-20261006/Release' / p) == h for p, h in build['release_sha256'].items())
profile_path = DATA / 'gc-generic-cycles-r5b-phase-diagnostic-20261009.json'
profile = read(profile_path)
assert profile['terminal'] and profile['status'] == 'diagnostic_completed'
assert profile['sources_and_releases_unchanged'] and all(p['external_process_watch']['measurement_valid'] for p in profile['phases'])
control = ROOT / 'build-repro/controls/gc-generic-cycles-r5b-failed-gate-20261009'
assert all(sha(control / 'sources' / p) == h for p, h in app['source_sha256'].items())
assert all(sha(control / 'Release' / p) == h for p, h in build['release_sha256'].items())
header = ROOT / 'src/runtime/modules/system/gc_plain_cycles.h'
source = header.read_text()
before = '    std::vector<std::vector<size_t>> parents(nodes.size()), children(nodes.size());'
after = '''    // One contiguous graph buffer avoids a tiny heap allocation for each
    // parent/child adjacency vector. Index links retain unique topology in
    // both directions; incoming ownership above still counts every Value.
    struct Edge { size_t source, target, next_parent, next_child; };
    constexpr size_t no_edge = std::numeric_limits<size_t>::max();
    std::vector<Edge> edges;
    edges.reserve(nodes.size());
    std::vector<size_t> parent_heads(nodes.size(), no_edge), child_heads(nodes.size(), no_edge);'''
assert source.count(before) == 1
source = source.replace(before, after)
before = '''          parents[target->second].push_back(source);
          children[source].push_back(target->second);
          node.last_parent = source;'''
after = '''          const size_t edge = edges.size();
          edges.push_back(Edge{source, target->second, parent_heads[target->second], child_heads[source]});
          parent_heads[target->second] = edge;
          child_heads[source] = edge;
          node.last_parent = source;'''
assert source.count(before) == 1
source = source.replace(before, after)
before = '''      for (size_t parent : parents[child]) {
        if (!nodes[parent].unsafe) { nodes[parent].unsafe = true; pending.push_back(parent); }
      }'''
after = '''      for (size_t edge = parent_heads[child]; edge != no_edge; edge = edges[edge].next_parent) {
        const size_t parent = edges[edge].source;
        if (!nodes[parent].unsafe) { nodes[parent].unsafe = true; pending.push_back(parent); }
      }'''
assert source.count(before) == 1
source = source.replace(before, after)
before = '''      for (size_t target : children[source]) {
        if (!nodes[target].reachable) {
          nodes[target].reachable = true; pending.push_back(target);
        }
      }'''
after = '''      for (size_t edge = child_heads[source]; edge != no_edge; edge = edges[edge].next_child) {
        const size_t target = edges[edge].target;
        if (!nodes[target].reachable) {
          nodes[target].reachable = true; pending.push_back(target);
        }
      }'''
assert source.count(before) == 1
source = source.replace(before, after)
header.write_text(source, encoding='utf-8', newline='\n')
current = {p: sha(ROOT / p) for p in app['source_sha256']}
assert {p for p in current if current[p] != app['source_sha256'][p]} == {'src/runtime/modules/system/gc_plain_cycles.h'}
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
app.update(status='generic_gc_flat_adjacency_r6_applied_validation_pending', source_sha256=current,
    integration_update={'parent_application_sha256': sha(old), 'phase_diagnostic_sha256': sha(profile_path),
        'reason': 'R5b owning-edge stage remains 643.8us; replace per-node adjacency-vector allocation and teardown with indexed arcs in one buffer. No refcount or root-admission changes.',
        'known_r5b_control': str(control), 'algorithm_changed': True}, controller_sha256=sha(__file__))
out = DATA / 'gc-generic-cycles-applied-source-r6-20261009.json'
assert not out.exists()
out.write_text(json.dumps(app, indent=2) + '\n', encoding='utf-8', newline='\n')
for prefix in ('build-gc-generic-cycles', 'check-gc-generic-cycles-correctness'):
    original = ROOT / ('scratch/performance/' + prefix + '-r5b-root-20261009.py')
    target = ROOT / ('scratch/performance/' + prefix + '-r6-root-20261009.py')
    text = original.read_text().replace(sha(old), sha(out))
    text = text.replace('applied-source-r5b-', 'applied-source-r6-').replace('build-r5b-', 'build-r6-')
    text = text.replace('correctness-r5b-20261009', 'correctness-r6-20261009')
    assert not target.exists()
    target.write_text(text, encoding='utf-8', newline='\n')
print(json.dumps({'application_sha256': sha(out), 'header_sha256': sha(header)}, indent=2))
