"""Strengthen test failure reporting before validation; engine algorithm unchanged."""
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
old = DATA / 'gc-generic-cycles-applied-source-r5-20261009.json'
app = json.loads(old.read_bytes())
assert sha(old) == '8583954a4c374328be8037949afa18404d5321a55c4996ece1a6bea4a9bdd8ca'
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
build = json.loads((DATA / 'gc-generic-cycles-build-r5-20261009.json').read_bytes())
assert build['terminal'] and build['passed'] and build['sources_unchanged']
archive = DATA / 'gc-generic-cycles-r5-first-build-inputs-20261009'
assert not archive.exists()
for name in ('src/runtime/modules/system/gc_plain_cycles.h', 'tests/cpp/generic_gc_cases.h'):
    target = archive / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / name, target)
    assert sha(target) == app['source_sha256'][name]
path = ROOT / 'tests/cpp/generic_gc_cases.h'
source = path.read_text()
before = '          value_as_list(items.front()) != nullptr && value_is(value_as_list(items.front())->items.front(), left),'
after = '''          value_as_list(items.front()) != nullptr && value_as_list(items.front())->items.size() == 1 &&
          value_is(value_as_list(items.front())->items.front(), left),'''
assert source.count(before) == 1
source = source.replace(before, after)
path.write_text(source, encoding='utf-8', newline='\n')
current = {p: sha(ROOT / p) for p in app['source_sha256']}
assert {p for p in current if current[p] != app['source_sha256'][p]} == {'tests/cpp/generic_gc_cases.h'}
app.update(status='generic_gc_packed_runs_r5b_test_guard_validation_pending', source_sha256=current,
    test_guard_update={'parent_application_sha256': sha(old), 'engine_algorithm_changed': False,
        'reason': 'Check preserved child storage length before front(), so a GC reachability regression reports a failed expectation rather than dereferencing an empty vector.'}, controller_sha256=sha(__file__))
out = DATA / 'gc-generic-cycles-applied-source-r5b-20261009.json'
assert not out.exists()
out.write_text(json.dumps(app, indent=2) + '\n', encoding='utf-8', newline='\n')
for prefix in ('build-gc-generic-cycles', 'check-gc-generic-cycles-correctness'):
    original = ROOT / ('scratch/performance/' + prefix + '-r5-root-20261009.py')
    target = ROOT / ('scratch/performance/' + prefix + '-r5b-root-20261009.py')
    text = original.read_text().replace(sha(old), sha(out))
    text = text.replace('applied-source-r5-', 'applied-source-r5b-').replace('build-r5-', 'build-r5b-')
    text = text.replace('correctness-r5-20261009', 'correctness-r5b-20261009')
    assert not target.exists()
    target.write_text(text, encoding='utf-8', newline='\n')
print(sha(out))
