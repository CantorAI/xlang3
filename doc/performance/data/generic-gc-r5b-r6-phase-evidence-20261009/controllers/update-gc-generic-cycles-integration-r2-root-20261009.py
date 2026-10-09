"""Fix DLL test inspection and explicit pass sequencing after the terminal link failure."""
import hashlib
import json
from pathlib import Path

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
app_path = DATA / 'gc-generic-cycles-applied-source-20261009.json'
assert sha(app_path) == '7b4b17e2ef51bbc3da59418bc72df9935f64dfd5df45f38a1081046c03c3f1b1'
app = json.loads(app_path.read_bytes())
build = json.loads((DATA / 'gc-generic-cycles-build-20261009.json').read_bytes())
assert build['terminal'] and not build['passed'] and build['sources_unchanged']
archive = DATA / 'gc-generic-cycles-first-build-inputs-20261009'
assert all(sha(archive / p) == app['source_sha256'][p] for p in app['owned_paths'])
current = {p: sha(ROOT / p) for p in app['source_sha256']}
assert {p for p in current if current[p] != app['source_sha256'][p]} == {
    'src/runtime/modules/system/gc_module.cpp', 'tests/cpp/generic_gc_cases.h'}
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
app.update(status='generic_gc_r2_applied_validation_pending', source_sha256=current,
    integration_update={'parent_application_sha256': sha(app_path),
        'terminal_failed_build_sha256': sha(DATA / 'gc-generic-cycles-build-20261009.json'),
        'reason': 'C++ DLL test referenced an internal unexported snapshot helper; inspect through registered gc.get_objects instead. Sequence specialized then plain passes with separate statements.',
        'algorithm_changed': False, 'first_inputs_exactly_preserved': True},
    controller_sha256=sha(__file__))
out = DATA / 'gc-generic-cycles-applied-source-r2-20261009.json'
assert not out.exists()
out.write_text(json.dumps(app, indent=2) + '\n', encoding='utf-8', newline='\n')
print(sha(out))
