"""Preserve accepted trace checkpoint and apply the held generic frame trial."""
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
BASELINE = ROOT / 'build-repro/Release'
CONTROL = ROOT / 'build-repro/controls/trace-disable-local-events-accepted-20261009'
PARENT = DATA / 'trace-disable-local-events-correctness-20261009.json'
GATE = DATA / 'trace-disable-local-events-performance-r2-20261009.json'
OLD_APP = DATA / 'trace-disable-local-events-applied-source-20261009.json'
PROPOSAL = ROOT / 'scratch/performance/runtime-frame-context-coalesced-proposed-20261008.patch'
OUT = DATA / 'frame-context-coalescing-applied-source-20261009.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda d: {p.relative_to(d).as_posix(): sha(p) for p in sorted(d.rglob('*')) if p.is_file()}
read = lambda p: json.loads(p.read_bytes())

def save(p, obj):
    p.write_text(json.dumps(obj, indent=2) + '\n', encoding='utf-8', newline='\n')

assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CP.resolve()
assert sha(PARENT) == '617a0841153d9f7e9620c7b23fb5c00ab55ad4e5c81b930a792e7ec773dc3e98'
assert sha(GATE) == '40ba17cb61f766b9cdaa02afc64b11e4a9d1a509f30f2c383e77a208af3e052f'
assert sha(PROPOSAL) == 'c7621f919aa075824a795b4e23dea610c48aa868bff22d45ab9524a9d6c8f2d9'
parent, gate, old_app = map(read, (PARENT, GATE, OLD_APP))
assert parent['correctness_passed'] and gate['fixed_gate']['passed'] and gate['hashes_unchanged']
assert len(parent['source_sha256']) == 137 and tree(RELEASE) == parent['release_sha256']
assert len(tree(RELEASE)) == 178 and len(tree(BASELINE)) == 177
assert tree(BASELINE) == old_app['fixed_baseline_sha256']
assert all(sha(ROOT / p) == h for p, h in parent['source_sha256'].items())
protected = old_app['unowned_tracked_dirty_sha256'].copy()
# Publication added owned attribute rules; preserve its actual whole working file now.
protected['.gitattributes'] = sha(ROOT / '.gitattributes')
assert all(sha(ROOT / p) == h for p, h in protected.items())
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == '95feaff27c2a42379f4bdaade27dc6afd13334d0'
assert not OUT.exists() and not CONTROL.exists()
existing = ['src/internal/xlang3/runtime.h', 'src/runtime/runtime.cpp',
    'src/executor/xlang_vm/xlang_vm_loop.cpp', 'tests/cpp/interpreter_tests.cpp',
    'tests/run_fixtures.py', 'tests/run_fixtures.ps1']
new_files = {'tests/cpp/runtime_frame_context_cases.h': 'runtime-frame-context-coalesced-cpp-proposed-20261008.h',
    'tests/fixtures/core/runtime_frame_context.py': 'runtime-frame-context-coalesced-fixture-proposed-20261008.py',
    'tests/fixtures/expected/runtime_frame_context.out': 'runtime-frame-context-coalesced-expected-proposed-20261008.out'}
assert all(not (ROOT / p).exists() for p in new_files)
CONTROL.mkdir()
shutil.copytree(RELEASE, CONTROL / 'Release')
for p in parent['source_sha256']:
    dst = CONTROL / 'sources' / p
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / p, dst)
assert tree(CONTROL / 'Release') == parent['release_sha256']
assert tree(CONTROL / 'sources') == parent['source_sha256']
save(CONTROL / 'preserved-release-provenance.json', {'status': 'accepted_trace_checkpoint_preserved',
    'head': '95feaff27c2a42379f4bdaade27dc6afd13334d0', 'correctness_sha256': sha(PARENT),
    'performance_sha256': sha(GATE), 'source_snapshot_sha256': parent['source_sha256'],
    'files_sha256': parent['release_sha256'], 'fixed_baseline_sha256': old_app['fixed_baseline_sha256'],
    'scope': '137 selected source inputs and complete Release178; partial worktree provenance, not a clean-checkout claim.'})

def replace(path, old, new):
    raw = (ROOT / path).read_bytes()
    nl = b'\r\n' if b'\r\n' in raw else b'\n'
    old, new = (s.replace('\n', nl.decode()).encode() for s in (old, new))
    assert raw.count(old) == 1, path
    (ROOT / path).write_bytes(raw.replace(old, new))

replace(existing[0], '  void push_current_frame_state();',
    '  // Update borrowed current-frame fields only while the globals owner is\n'
    '  // unchanged; false leaves the original setter sequence responsible.\n'
    '  bool try_set_current_frame_from_stack();\n  void push_current_frame_state();')
definition = '''bool Runtime::try_set_current_frame_from_stack() {
  auto& state = current_frame_state(*this);
  if (state.frame_stack == nullptr || state.frame_stack_count == 0) return false;
  const auto& frame = state.frame_stack[state.frame_stack_count - 1];
  if (frame.module_owner == nullptr || frame.globals_module == nullptr ||
      frame.instruction_index == nullptr ||
      state.current_globals_module.tag != ValueTag::Object ||
      frame.globals_module->tag != ValueTag::Object ||
      frame.globals_module->as.obj == nullptr ||
      state.current_globals_module.as.obj != frame.globals_module->as.obj) return false;
  // The VM refreshed this physical view under the execution lock. Coalesce
  // three exported setters/TLS guards, retaining their locals-then-frame order.
  // No owning Value is touched here: identical globals already have an owner,
  // so this path cannot finalize or reenter. Changed globals must keep the
  // original setter sequence and its cleanup ordering. Do not omit frame
  // publication or reuse an owning inspection snapshot to extend this path.
  state.local_names = frame.local_names;
  state.local_values = frame.local_values;
  state.local_count = frame.local_count;
  state.module_owner = frame.module_owner;
  state.function_id = frame.function_id;
  state.globals_module = frame.globals_module;
  state.instruction_index = static_cast<uint32_t>(*frame.instruction_index);
  return true;
}

'''
replace(existing[1], 'void Runtime::push_current_frame_state() {', definition + 'void Runtime::push_current_frame_state() {')
replace(existing[2], '''    runtime_.set_current_globals_module(globals_module);
    runtime_.set_current_frame_locals(&fn.locals, locals.value_data(), locals.size());
    // The live frame-stack view owns the changing instruction pointer. Update
    // the legacy single-frame identity once when execution switches frames.
    runtime_.set_current_frame(&module_owner, frame.function_id, &globals_module, ip);''',
'''    if (!runtime_.try_set_current_frame_from_stack()) {
      // Preserve changed-globals cleanup/reentry ordering. Evaluate the locals
      // pointers only after any old-owner finalizer has returned.
      runtime_.set_current_globals_module(globals_module);
      runtime_.set_current_frame_locals(&fn.locals, locals.value_data(), locals.size());
      runtime_.set_current_frame(&module_owner, frame.function_id, &globals_module, ip);
    }''')
replace(existing[3], '#include "class_method_annotation_capture_cases.h"',
    '#include "class_method_annotation_capture_cases.h"\n#include "runtime_frame_context_cases.h"')
replace(existing[3], '  xlang3::test::check_class_method_annotation_capture(result);',
    '  xlang3::test::check_class_method_annotation_capture(result);\n  xlang3::test::check_runtime_frame_context(result);')
replace(existing[4], 'sorted_exact_integer_keys\n', 'sorted_exact_integer_keys\nruntime_frame_context\n')
replace(existing[5], '    "sorted_exact_integer_keys",', '    "sorted_exact_integer_keys",\n    "runtime_frame_context",')
for p, name in new_files.items():
    shutil.copyfile(ROOT / 'scratch/performance' / name, ROOT / p)
sources = {p: sha(ROOT / p) for p in parent['source_sha256']}
sources.update({p: sha(ROOT / p) for p in new_files})
assert {p for p in parent['source_sha256'] if sources[p] != parent['source_sha256'][p]} == set(existing)
assert all(sha(ROOT / p) == h for p, h in protected.items())
save(OUT, {'terminal': True, 'status': 'trial_applied_validation_pending',
    'head': '95feaff27c2a42379f4bdaade27dc6afd13334d0', 'source_count': len(sources),
    'source_sha256': sources, 'owned_paths': existing + list(new_files),
    'unowned_tracked_dirty_sha256': protected, 'fixed_baseline_sha256': old_app['fixed_baseline_sha256'],
    'parent_release_sha256': parent['release_sha256'], 'parent_preserved': str(CONTROL / 'preserved-release-provenance.json'),
    'parent_preserved_sha256': sha(CONTROL / 'preserved-release-provenance.json'),
    'held_proposal_sha256': sha(PROPOSAL), 'controller_sha256': sha(__file__),
    'scope': 'Coalesce borrowed legacy frame fields only; unchanged owner, inspection publication and fallback semantics. No IR shape or pure-Python library change.',
    'engine_commit_permitted': False})
print(json.dumps({'status': 'applied', 'source_count': len(sources), 'receipt_sha256': sha(OUT)}, indent=2))
