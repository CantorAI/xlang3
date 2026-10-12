"""Reuse exact accepted control; preserve added inputs and apply generic GC repair."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CONTROL = ROOT / 'build-repro/controls/trace-disable-local-events-accepted-20261009'
RESTORED = DATA / 'frame-context-coalescing-rejected-restored-20261009.json'
OUT = DATA / 'gc-generic-cycles-applied-source-20261009.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda d: {p.relative_to(d).as_posix(): sha(p) for p in sorted(d.rglob('*')) if p.is_file()}
read = lambda p: json.loads(p.read_bytes())
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7)
assert sha(RESTORED) == '69d0a50979c12f8cdb31d6f62e8ca625345e1ead6f2b7fbf10c7802e533668e4'
assert sha(CONTROL / 'preserved-release-provenance.json') == 'ef216326f493909b1551c2fe69b0c1eaaaa828db6eb8550a06dc5198bc80920b'
restored, control = map(read, (RESTORED, CONTROL / 'preserved-release-provenance.json'))
assert tree(RELEASE) == tree(CONTROL / 'Release') == restored['restored_release_sha256']
assert tree(CONTROL / 'sources') == restored['restored_source_sha256']
assert all(sha(ROOT / p) == h for p, h in restored['restored_source_sha256'].items())
assert tree(ROOT / 'build-repro/Release') == restored['fixed_baseline_sha256']
protected = restored['unowned_tracked_dirty_sha256'].copy()
protected['.gitattributes'] = read(DATA / 'frame-context-coalescing-publication-20261009.json')['working_attributes_after_sha256']
assert all(sha(ROOT / p) == h for p, h in protected.items())
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
assert head == 'aef4dc0b1f81644d693c0684fa5d35aa8038a9ee'
existing = ['src/runtime/modules/system/gc_module.cpp', 'tests/cpp/interpreter_tests.cpp', 'tests/run_fixtures.py', 'tests/run_fixtures.ps1']
new = {'src/runtime/modules/system/gc_plain_cycles.h': 'gc-generic-cycles-proposed-20261009.h',
    'tests/cpp/generic_gc_cases.h': 'gc-generic-cycles-cpp-proposed-20261009.h',
    'tests/fixtures/core/gc_generic_cycles.py': 'gc-generic-cycles-fixture-proposed-20261009.py'}
expected_path = 'tests/fixtures/expected/gc_generic_cycles.out'
assert not OUT.exists() and all(not (ROOT / p).exists() for p in [*new, expected_path])
before = restored['restored_source_sha256'].copy()
before.update({p: sha(ROOT / p) for p in existing})
archive = DATA / 'gc-generic-cycles-before-20261009'
assert not archive.exists()
for p in existing:
    target = archive / p
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / p, target)
expected = ('PASS ordinary-instance-container-slot-cycles\nPASS reachable-cycle-and-tail\n'
    'PASS reachable-finalizer-boundary\nPASS other-thread-frame-root\nPASS handled-exception-preserved\n')
reference = subprocess.run([str(CP), '-I', str(ROOT / 'scratch/performance' / new['tests/fixtures/core/gc_generic_cycles.py'])], cwd=ROOT, capture_output=True, check=True)
assert reference.stdout.decode().replace('\r', '') == expected and not reference.stderr
def replace(path, old, new_text):
    path = ROOT / path
    raw = path.read_bytes(); nl = b'\r\n' if b'\r\n' in raw else b'\n'
    old, new_text = (s.replace('\n', nl.decode()).encode() for s in (old, new_text))
    assert raw.count(old) == 1, str(path)
    path.write_bytes(raw.replace(old, new_text))
replace(existing[0], '#include "xlang3/set_object.h"', '#include "xlang3/set_object.h"\n#include "gc_plain_cycles.h"')
replace(existing[0], '#include <algorithm>', '#include <algorithm>\n#include <atomic>')
replace(existing[0], 'struct GcState {', '''std::atomic_bool& gc_collection_active() {
  static std::atomic_bool active{false};
  return active;
}

struct GcCollectionScope {
  ~GcCollectionScope() { gc_collection_active().store(false, std::memory_order_release); }
};

struct GcState {''')
replace(existing[0], '''  runtime.synchronize_modules_from_registry();
  runtime.release_dead_frame_registers();
  emit_pending_socket_resource_warnings(runtime);
  const uint64_t collected = weakref_collect_cycles(runtime);''', '''  // Finalizers/native cleanup can call gc.collect recursively or from another
  // thread. Keep one process-wide collection active through both passes and
  // snapshot retirement; reentry must not inspect a partly retired graph.
  if (gc_collection_active().exchange(true, std::memory_order_acq_rel)) {
    value_set_int64(out, 0);
    return true;
  }
  GcCollectionScope collection_scope;
  runtime.synchronize_modules_from_registry();
  runtime.release_dead_frame_registers();
  emit_pending_socket_resource_warnings(runtime);
  const uint64_t collected = weakref_collect_cycles(runtime) + gc_collect_plain_cycles();''')
replace(existing[1], '#include "dict_scalar_append_index_cases.h"', '#include "dict_scalar_append_index_cases.h"\n#include "generic_gc_cases.h"')
replace(existing[1], '  xlang3::test::check_dict_scalar_append_index_cases(result);', '  xlang3::test::check_dict_scalar_append_index_cases(result);\n  xlang3::test::check_generic_gc_cases(result);')
replace(existing[2], 'gc_rooted_candidate_graph\n', 'gc_rooted_candidate_graph\ngc_generic_cycles\n')
replace(existing[3], '    "gc_rooted_candidate_graph",', '    "gc_rooted_candidate_graph",\n    "gc_generic_cycles",')
for p, name in new.items(): shutil.copyfile(ROOT / 'scratch/performance' / name, ROOT / p)
(ROOT / expected_path).write_text(expected, encoding='utf-8', newline='\n')
sources = {p: sha(ROOT / p) for p in before}
sources.update({p: sha(ROOT / p) for p in [*new, expected_path]})
assert {p for p in before if sources[p] != before[p]} == set(existing)
assert all(sha(ROOT / p) == h for p, h in protected.items())
record = {'terminal': True, 'status': 'generic_gc_applied_validation_pending', 'head': head,
    'source_count': len(sources), 'source_sha256': sources, 'parent_source_sha256': before,
    'owned_paths': existing + [*new, expected_path], 'unowned_tracked_dirty_sha256': protected,
    'fixed_baseline_sha256': restored['fixed_baseline_sha256'], 'parent_release_sha256': restored['restored_release_sha256'],
    'parent_preserved': str(CONTROL / 'preserved-release-provenance.json'), 'parent_preserved_sha256': sha(CONTROL / 'preserved-release-provenance.json'),
    'extra_parent_input_archive_sha256': tree(archive), 'proposals_sha256': {name: sha(ROOT / 'scratch/performance' / name) for name in new.values()},
    'cp_reference': {'exit_code': reference.returncode, 'stdout': reference.stdout.decode(), 'stderr': reference.stderr.decode()},
    'controller_sha256': sha(__file__), 'engine_commit_permitted': False,
    'scope': 'Discover ordinary instance/container/cell cycles with exact pin/edge accounting. Preserve roots and callback boundaries; cyclic finalizer/native/opaque handling remains in specialized/conservative paths. No pure-Python library translation.'}
OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({'status': record['status'], 'source_count': len(sources), 'receipt_sha256': sha(OUT)}, indent=2))
