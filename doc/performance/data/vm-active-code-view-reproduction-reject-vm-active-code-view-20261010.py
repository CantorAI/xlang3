"""Preserve the neutral trial and restore exact accepted bytes; no rebuild."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
CONTROL = ROOT / 'build-repro/controls/gc-generic-cycles-r7b-accepted-20261009'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
load = lambda p: json.loads(Path(p).read_bytes())
out = DATA / 'vm-active-code-view-rejected-20261010.json'
assert not out.exists()
receipt = DATA / 'vm-active-code-view-r5-validation-20261009-validation.json'
assert sha(receipt) == '5d4636c731ba205b51e987f3ff70bace9691310685c5d6ea196fa9114624be39'
assert sha(CONTROL / 'provenance.json') == 'aa2d9f715ad11e78ffa58024c10631c37e7d83d816c4a8cb0523f6469287547f'
r, c = load(receipt), load(CONTROL / 'provenance.json')
b = load(DATA / 'vm-active-code-view-r2-20261009-build.json')
a = load(DATA / 'vm-active-code-view-r2-applied-source-20261009.json')
assert r['terminal'] and r['correctness_passed'] and r['fixed_gate']['passed']
assert r['status'] == 'correctness_gate_and_original_json_completed'
for path, h in a['source_sha256'].items():
    assert sha(ROOT / path) == h, path
for path, h in b['release_sha256'].items():
    assert sha(RELEASE / path) == h, path
for path, h in c['unowned_tracked_dirty_sha256'].items():
    assert sha(ROOT / path) == h, path
for path, h in c['release_sha256'].items():
    assert sha(CONTROL / 'Release' / path) == h, path
for path, h in c['fixed_baseline_sha256'].items():
    assert sha(ROOT / 'build-repro/Release' / path) == h, path
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT).decode().strip() == 'a983bb2eeca83a1480090222693feac7b6aab208'

preserved = {}
for path in ('vm-active-code-view-hoist-proposed-20261009.patch',
             'vm-active-code-view-test-registration-proposed-20261009.patch'):
    src = ROOT / 'scratch/performance' / path
    dst = DATA / path
    assert not dst.exists()
    shutil.copyfile(src, dst)
    preserved[str(dst.relative_to(ROOT))] = sha(dst)
for path in ('tests/fixtures/core/vm_active_code_view.py', 'tests/fixtures/expected/vm_active_code_view.out'):
    src = ROOT / path
    dst = DATA / ('vm-active-code-view-rejected-fixture-20261010' + src.suffix)
    assert not dst.exists()
    shutil.copyfile(src, dst)
    preserved[str(dst.relative_to(ROOT))] = sha(dst)

restored = {}
for path in ('src/executor/xlang_vm/xlang_vm_loop.cpp', 'tests/run_fixtures.py'):
    src = CONTROL / 'sources' / path
    assert sha(src) == c['source_sha256'][path]
    shutil.copyfile(src, ROOT / path)
    restored[path] = sha(ROOT / path)
# The accepted control predates the committed UTF-8 harness repair.
path = 'tests/run_fixtures.ps1'
raw = subprocess.check_output(['git', 'show', '784888a05aa14a22b04942c59e9b96199475bcb5:' + path], cwd=ROOT)
# Working-tree CRLF bytes are separately authenticated by the repair receipt.
harness = load(DATA / 'gc-r7b-full-ctest-harness-repair-20261009-receipt.json')
expected = b['harness_repair_source_sha256'][path]
if hashlib.sha256(raw).hexdigest() != expected:
    raw = raw.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
assert hashlib.sha256(raw).hexdigest() == expected
(ROOT / path).write_bytes(raw)
restored[path] = sha(ROOT / path)
for path in ('tests/fixtures/core/vm_active_code_view.py', 'tests/fixtures/expected/vm_active_code_view.out'):
    assert sha(ROOT / path) == a['source_sha256'][path]
    (ROOT / path).unlink()
for path, h in c['release_sha256'].items():
    if sha(RELEASE / path) != h:
        shutil.copyfile(CONTROL / 'Release' / path, RELEASE / path)
    assert sha(RELEASE / path) == h
for path, h in c['source_sha256'].items():
    assert sha(ROOT / path) == (expected if path == 'tests/run_fixtures.ps1' else h), path
for path, h in c['unowned_tracked_dirty_sha256'].items():
    assert sha(ROOT / path) == h, path
for path, h in c['fixed_baseline_sha256'].items():
    assert sha(ROOT / 'build-repro/Release' / path) == h, path
record = dict(terminal=True, status='rejected_no_demonstrated_gain_restored',
    reason='Original unchanged json_dumps has no demonstrated gain: sequential means are 35.806889 ms control and 35.996586 ms candidate. Gate tolerance is not evidence of benefit.',
    validation_receipt=str(receipt.relative_to(ROOT)), validation_sha256=sha(receipt),
    original_json=r['original_json'], unpaired_control_over_candidate=r['unpaired_control_over_candidate'],
    preserved=preserved, restored_source_sha256=restored,
    restored_release_sha256=c['release_sha256'], unowned_unchanged=True, fixed_baseline_unchanged=True,
    controller_sha256=sha(__file__),
    limit='No post-restore rebuild or timing. Candidate build objects remain on disk; a future build must rebuild changed source. Historical validation remains scoped to its original candidate.')
out.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
print(record['status'], sha(out), flush=True)
