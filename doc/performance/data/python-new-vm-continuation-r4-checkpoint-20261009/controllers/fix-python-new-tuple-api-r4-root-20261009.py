"""Correct two rejected-build TupleItems calls; retain exact R3 ancestry."""
import difflib
import hashlib
import json
from pathlib import Path
import subprocess
import sys
ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
SCRATCH = ROOT / 'scratch/performance'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CONTROL = ROOT / 'build-repro/controls/lambda-eager-comprehension-capture-accepted-r5-20261009'
APP = DATA / 'python-new-vm-continuation-r3-applied-source-20261009.json'
FAILED = DATA / 'python-new-vm-continuation-r3-build-20261009.json'
OUT = DATA / 'python-new-vm-continuation-r4-applied-source-20261009.json'
PATCH = SCRATCH / 'python-new-vm-continuation-r4-tuple-api-root-20261009.patch'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated and not sys.flags.optimize
assert sha(APP) == 'efbc1498c2475aaf2013c283c4de85b164d4ee4457b4f2bbe7138934119bd635'
assert sha(FAILED) == '6cf6617a1a0dd7587fc16be4d2e68263e5ce03c7d962d23915bd7c62ff6a6a58'
app, failed = json.loads(APP.read_bytes()), json.loads(FAILED.read_bytes())
assert failed['terminal'] and not failed['passed'] and failed['exit_code'] == 2
manifest = CONTROL / 'preserved-release-provenance.json'
assert sha(manifest) == '9271314856ede7f173396b2f39449a01d785f61a1579ce5f93c7786a2961dfad'
saved = json.loads(manifest.read_bytes())
assert all(sha(ROOT / n) == h for n, h in (app['source_sha256'] | app['unowned_tracked_dirty_sha256']).items())
assert tree(RELEASE) == tree(CONTROL / 'Release') == saved['files_sha256']
assert tree(ROOT / 'build-repro/Release') == saved['fixed_baseline_sha256']
assert not OUT.exists() and not PATCH.exists()
assert not subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT)
changes = {
    'src/executor/xlang_vm/ops/xlang_vm_ops_call.h': (b'tuple->items.data() + 1', b'tuple->items.begin() + 1'),
    'src/executor/xlang_vm/xlang_vm_loop.cpp': (b'context->items.data() + 2', b'context->items.begin() + 2'),
}
after_bytes, diffs = {}, []
for n, (old, new) in changes.items():
    before = (ROOT / n).read_bytes()
    assert before.count(old) == 1
    after = before.replace(old, new)
    after_bytes[n] = after
    preserved = SCRATCH / ('python-new-r4-before-' + Path(n).name)
    assert not preserved.exists()
    preserved.write_bytes(before)
    diffs.extend(difflib.unified_diff(before.decode().splitlines(keepends=True), after.decode().splitlines(keepends=True), fromfile='a/' + n, tofile='b/' + n))
PATCH.write_bytes(''.join(diffs).encode())
subprocess.run(['git', 'apply', '--check', str(PATCH)], cwd=ROOT, capture_output=True, check=True)
for n, b in after_bytes.items():
    (ROOT / n).write_bytes(b)
record = dict(app)
updated = {n: sha(ROOT / n) for n in changes}
record.update(status='applied_python_new_r4_tuple_api_correction',
              source_sha256=app['source_sha256'] | updated,
              targets_sha256=app['targets_sha256'] | updated,
              r3_application_sha256=sha(APP), rejected_build_sha256=sha(FAILED),
              correction_patch=str(PATCH.relative_to(ROOT)), correction_patch_sha256=sha(PATCH),
              controller_sha256=sha(__file__), build_pending=True)
assert all(sha(ROOT / n) == h for n, h in (record['source_sha256'] | record['unowned_tracked_dirty_sha256']).items())
assert tree(RELEASE) == saved['files_sha256'] and tree(ROOT / 'build-repro/Release') == saved['fixed_baseline_sha256']
OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print(record['status'], 'receipt', sha(OUT))
# Preserve the already executed R3 builder; generate a separate R4 command.
builder = SCRATCH / 'build-python-new-vm-continuation-r3-root-20261009.py'
command = SCRATCH / 'build-python-new-vm-continuation-r3-root-20261009.cmd'
new_builder = SCRATCH / 'build-python-new-vm-continuation-r4-root-20261009.py'
new_command = SCRATCH / 'build-python-new-vm-continuation-r4-root-20261009.cmd'
assert not new_builder.exists() and not new_command.exists()
new_command.write_bytes(command.read_bytes())
new_builder.write_bytes(builder.read_bytes().replace(b'python-new-vm-continuation-r3', b'python-new-vm-continuation-r4').replace(sha(APP).encode(), sha(OUT).encode()))
