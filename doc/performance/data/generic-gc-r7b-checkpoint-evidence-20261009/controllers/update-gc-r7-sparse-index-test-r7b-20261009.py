"""Strengthen sparse-index coverage after the fresh R7 checks completed."""
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
old = DATA / 'gc-generic-cycles-applied-source-r7-20261009.json'
assert sha(old) == 'faf9512e0a2ce39b84747eb23f7f10e36789d7cf328d7a0c7620d1b14002945c'
app = read(old)
correct = read(DATA / 'gc-generic-cycles-correctness-r7-20261009.json')
assert correct['terminal'] and correct['correctness_passed'] and correct['sources_unchanged'] and correct['release_unchanged']
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
archive = DATA / 'gc-generic-cycles-r7-first-correctness-inputs-20261009'
assert not archive.exists()
archive.mkdir()
for name in ('src/runtime/modules/system/gc_plain_node_index.h', 'src/runtime/modules/system/gc_plain_cycles.h', 'tests/cpp/generic_gc_cases.h'):
    target = archive / name
    target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(ROOT / name, target)
    assert sha(target) == app['source_sha256'][name]
test = ROOT / 'tests/cpp/generic_gc_cases.h'
source = test.read_text()
before = '''    std::vector<Node> empty;
    gc_detail::PlainNodeIndex<Node> empty_index(empty, 0);'''
after = '''    // The sentinel above exits on the first node. Exercise the huge-index
    // branch separately with a valid first slot, without allocating its extent.
    first.gc_tracking_state.store(1);
    gc_detail::PlainNodeIndex<Node> huge_sparse(nodes, 2);
    expect_true(result, !huge_sparse.uses_dense_table() && huge_sparse.find(&first) == 0 &&
        huge_sparse.find(&second) == 1,
        "a huge later slot selects sparse lookup after a valid earlier slot");
    std::vector<Node> empty;
    gc_detail::PlainNodeIndex<Node> empty_index(empty, 0);'''
assert source.count(before) == 1
test.write_text(source.replace(before, after), encoding='utf-8', newline='\n')
current = {p: sha(ROOT / p) for p in app['source_sha256']}
assert {p for p in current if current[p] != app['source_sha256'][p]} == {'tests/cpp/generic_gc_cases.h'}
app.update(status='generic_gc_bounded_node_index_r7b_test_strengthened_validation_pending', source_sha256=current,
    integration_update={'parent_application_sha256': sha(old),
        'reason': 'Independent review found the first-node sentinel prevented the huge second slot from executing. Add a separate valid-first/huge-second test. Engine bytes unchanged; no previous timing launched.',
        'engine_changed': False, 'correctness_parent_sha256': sha(DATA / 'gc-generic-cycles-correctness-r7-20261009.json')},
    controller_sha256=sha(__file__))
out = DATA / 'gc-generic-cycles-applied-source-r7b-20261009.json'
assert not out.exists()
out.write_text(json.dumps(app, indent=2) + '\n', encoding='utf-8', newline='\n')
for prefix in ('build-gc-generic-cycles', 'check-gc-generic-cycles-correctness'):
    original = ROOT / ('scratch/performance/' + prefix + '-r7-root-20261009.py')
    target = ROOT / ('scratch/performance/' + prefix + '-r7b-root-20261009.py')
    source = original.read_text().replace(sha(old), sha(out)).replace('-r7-', '-r7b-')
    assert not target.exists()
    target.write_text(source, encoding='utf-8', newline='\n')
print('R7b application', sha(out))
