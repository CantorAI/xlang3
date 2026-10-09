"""Preserve the built trace candidate, prove the failure, then repair dispatch."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BASELINE = ROOT / 'build-repro/Release'
CONTROL = ROOT / 'build-repro/controls/trace-setting-first-correctness-control-20261009'
PREVIOUS_APP = DATA / 'nested-trace-setting-applied-source-20261009.json'
PREVIOUS_CORRECT = DATA / 'nested-trace-setting-correctness-r2-20261009.json'
PROPOSED = ROOT / 'scratch/performance/trace-disable-local-events-fixture-proposed-20261009.py'
OUT = DATA / 'trace-disable-local-events-applied-source-20261009.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda d: {p.relative_to(d).as_posix(): sha(p) for p in d.rglob('*') if p.is_file()}
def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8', newline='\n')
def insert(path, anchor, addition):
    raw = path.read_bytes()
    newline = b'\r\n' if b'\r\n' in raw else b'\n'
    old = anchor.encode() + newline
    assert raw.count(old) == 1
    path.write_bytes(raw.replace(old, old + addition.encode() + newline))
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve() and sys.version_info[:3] == (3, 14, 7)
assert sha(PREVIOUS_APP) == '3fc7e9f94c1bc238da76a561e85fe030dfdfdbdbe7a06d0a697166696f5dadd5'
assert sha(PREVIOUS_CORRECT) == 'de6a31342817c406d103cc9811fc5d01f82e036bf4cb6c0495ac1e3e1743619b'
app, correct = (json.loads(p.read_bytes()) for p in (PREVIOUS_APP, PREVIOUS_CORRECT))
assert correct['status'] == 'correctness_passed_performance_pending'
assert correct['source_count'] == len(correct['source_sha256']) == 135
assert all(sha(ROOT / p) == h for p, h in correct['source_sha256'].items())
assert tree(RELEASE) == correct['release_sha256'] and len(tree(RELEASE)) == 178
assert tree(BASELINE) == app['fixed_baseline_sha256'] and len(tree(BASELINE)) == 177
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
assert not CONTROL.exists() and not OUT.exists()
new_fixture = 'tests/fixtures/core/trace_disable_local_events.py'
new_expected = 'tests/fixtures/expected/trace_disable_local_events.out'
assert not (ROOT / new_fixture).exists() and not (ROOT / new_expected).exists()
probe = []
for label, exe, extra in (('cpython3147', Path('C:/Python/Python314/python.exe'), ['-I']),
                         ('previous_trace_candidate', RELEASE / 'xlang3.exe', [])):
    result = subprocess.run([str(exe), *extra, str(PROPOSED)], cwd=ROOT, capture_output=True, timeout=30)
    probe.append({'label': label, 'command': [str(exe), *extra, str(PROPOSED)], 'exit_code': result.returncode,
                  'stdout': result.stdout.decode('utf-8'), 'stderr': result.stderr.decode('utf-8')})
assert all(p['exit_code'] == 0 for p in probe)
expected = "off 7 ['outer_off:call', 'disable:call'] []\nresume 8 ['outer_resume:call', 'disable:call', 'marker:call', 'marker:return', 'outer_resume:return'] []\n"
assert probe[0]['stdout'].replace('\r', '') == expected
assert probe[1]['stdout'].replace('\r', '') != expected and 'outer_off:line' in probe[1]['stdout']
CONTROL.mkdir()
shutil.copytree(RELEASE, CONTROL / 'Release')
for p in correct['source_sha256']:
    dst = CONTROL / 'sources' / p
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / p, dst)
assert tree(CONTROL / 'Release') == correct['release_sha256'] and tree(CONTROL / 'sources') == correct['source_sha256']
save(CONTROL / 'preserved-release-provenance.json', {'status': 'correctness_only_trace_candidate_preserved',
    'correctness_sha256': sha(PREVIOUS_CORRECT), 'files_sha256': correct['release_sha256'],
    'source_snapshot_sha256': correct['source_sha256'], 'fixed_baseline_sha256': app['fixed_baseline_sha256'],
    'performance_gate_passed': False, 'scope': '135 selected source inputs and Release178; not an accepted baseline or clean-checkout claim.'})
vm = ROOT / 'src/executor/xlang_vm/xlang_vm_loop.cpp'
old = '''    const Value& borrowed_hook = is_call_event ? runtime_.trace_function() : trace_frame.trace_function;
    if (borrowed_hook.tag == ValueTag::Invalid || borrowed_hook.tag == ValueTag::None || runtime_.trace_dispatch_active()) {'''
new = '''    const Value& thread_hook = runtime_.trace_function();
    const Value& borrowed_hook = is_call_event ? thread_hook : trace_frame.trace_function;
    // sys.settrace(None) suspends every trace event, including existing local
    // hooks. Keep local hooks intact for reactivation, but never dispatch them
    // after global tracing is disabled (Coverage relies on balanced returns).
    // This check stays in event dispatch, outside ordinary opcode execution;
    // trace_event_may_dispatch is only a sticky capability hint, not enablement.
    if (thread_hook.tag == ValueTag::Invalid || thread_hook.tag == ValueTag::None ||
        borrowed_hook.tag == ValueTag::Invalid || borrowed_hook.tag == ValueTag::None || runtime_.trace_dispatch_active()) {'''
raw = vm.read_bytes()
newline = b'\r\n' if b'\r\n' in raw else b'\n'
old_bytes, new_bytes = (s.replace('\n', newline.decode()).encode() for s in (old, new))
assert raw.count(old_bytes) == 1
vm.write_bytes(raw.replace(old_bytes, new_bytes))
shutil.copyfile(PROPOSED, ROOT / new_fixture)
(ROOT / new_expected).write_text(expected, encoding='utf-8', newline='\n')
insert(ROOT / 'tests/run_fixtures.py', 'nested_trace_setting', 'trace_disable_local_events')
insert(ROOT / 'tests/run_fixtures.ps1', '    "nested_trace_setting",', '    "trace_disable_local_events",')
source_after = {p: sha(ROOT / p) for p in correct['source_sha256']}
source_after.update({p: sha(ROOT / p) for p in (new_fixture, new_expected)})
assert {p for p in correct['source_sha256'] if source_after[p] != correct['source_sha256'][p]} == {
    'src/executor/xlang_vm/xlang_vm_loop.cpp', 'tests/run_fixtures.py', 'tests/run_fixtures.ps1'}
assert tree(RELEASE) == correct['release_sha256'] and tree(BASELINE) == app['fixed_baseline_sha256']
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
assert head == app['head']
save(OUT, {'terminal': True, 'status': 'combined_trace_repairs_applied_validation_pending', 'head': head,
    'source_count': len(source_after), 'source_sha256': source_after,
    'owned_paths': app['owned_paths'] + [new_fixture, new_expected],
    'unowned_tracked_dirty_sha256': app['unowned_tracked_dirty_sha256'],
    'fixed_baseline_sha256': app['fixed_baseline_sha256'], 'parent_release_sha256': correct['release_sha256'],
    'parent_preserved_sha256': sha(CONTROL / 'preserved-release-provenance.json'),
    'parent_preserved': str(CONTROL / 'preserved-release-provenance.json'),
    'minimal_cp_x_proof': probe, 'fixture_proposal_sha256': sha(PROPOSED), 'controller_sha256': sha(__file__),
    'superseded_unexecuted_checks': ['validate-nested-trace-setting-performance-r2-root-20261009.py', 'measure-object-new-route-root-20261009.py'],
    'supersession_reason': 'New generic trace-disable regression proven before starting timings; do not time obsolete source135 candidate.',
    'engine_commit_permitted': False})
print(json.dumps({'status': 'applied', 'source_count': len(source_after), 'receipt_sha256': sha(OUT)}, indent=2))
