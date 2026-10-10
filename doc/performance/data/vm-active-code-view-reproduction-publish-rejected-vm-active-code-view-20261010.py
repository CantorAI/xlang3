"""Publish only the measured trial's documentation and exact retained bytes."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

root = Path('D:/CantorAI/xlang3')
data = root / 'doc/performance/data'
scratch = root / 'scratch/performance'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
git = lambda *a, **kw: subprocess.check_output(['git', *a], cwd=root, **kw)
assert git('rev-parse', 'HEAD').decode().strip() == 'a983bb2eeca83a1480090222693feac7b6aab208'
assert not git('diff', '--cached', '--name-only').strip()
reject = data / 'vm-active-code-view-rejected-20261010.json'
assert sha(reject) == '92ad1b231b87c847cfdd49f89262ca5bccd280f7c4ed6a83253347768475f214'
record = json.loads(reject.read_bytes())
assert record['terminal'] and record['status'] == 'rejected_no_demonstrated_gain_restored'
control = json.loads((root / 'build-repro/controls/gc-generic-cycles-r7b-accepted-20261009/provenance.json').read_bytes())
for p, h in control['unowned_tracked_dirty_sha256'].items():
    assert sha(root / p) == h
for p, h in record['restored_source_sha256'].items():
    assert sha(root / p) == h

copies = ['build-vm-active-code-view-proposed-20261009.py',
    'build-vm-active-code-view-root-r2-20261009.py',
    'validate-vm-active-code-view-proposed-20261009.py',
    'validate-vm-active-code-view-r2-proposed-20261009.py',
    'validate-vm-active-code-view-r3-proposed-20261009.py',
    'validate-vm-active-code-view-r4-untimed-resume-proposed-20261009.py',
    'validate-vm-active-code-view-r5-doc-head-resume-proposed-20261009.py',
    'wait-vm-active-code-view-r3-validation-20261009.py',
    'reject-vm-active-code-view-20261010.py',
    'publish-rejected-vm-active-code-view-20261010.py',
    'vm-active-code-view-hoist-provenance-proposed-20261009.json',
    'vm-active-code-view-r2-validation-provenance-proposed-20261009.json',
    'vm-active-code-view-r3-validation-provenance-proposed-20261009.json',
    'vm-active-code-view-r4-untimed-resume-provenance-proposed-20261009.json',
    'vm-active-code-view-r5-doc-head-resume-provenance-proposed-20261009.json',
    'vm-active-code-view-trial-controllers-root-static-review-20261009.json']
for name in copies:
    dst = data / ('vm-active-code-view-reproduction-' + name)
    assert not dst.exists()
    shutil.copyfile(scratch / name, dst)
    assert sha(dst) == sha(scratch / name)
dst = data / 'vm-active-code-view-reproduction-build-command-20261009.cmd'
assert not dst.exists()
shutil.copyfile(scratch / 'build-python-new-vm-continuation-r4-root-20261009.cmd', dst)
paths = sorted(p for p in data.glob('vm-active-code-view*') if p.is_file())
paths += [root / 'doc/performance/.gitattributes', root / 'doc/performance/vm-active-code-view-trial-20261010.md']
manifest = data / 'vm-active-code-view-publication-manifest-20261010.json'
assert not manifest.exists()
manifest.write_text(json.dumps(dict(scope='Rejected trial evidence only; no engine or test source changes.',
    rejection_sha256=sha(reject), controller_sha256=sha(__file__),
    files={p.relative_to(root).as_posix(): sha(p) for p in paths}), indent=2) + '\n', encoding='utf-8', newline='\n')
paths.append(manifest)
for p in paths:
    rel = p.relative_to(root).as_posix()
    assert rel.startswith('doc/performance/')
    blob = git('hash-object', '-w', '--stdin', input=p.read_bytes()).decode().strip()
    git('update-index', '--add', '--cacheinfo', '100644', blob, rel)
    assert git('rev-parse', ':' + rel).decode().strip() == blob
staged = git('diff', '--cached', '--name-only', '-z').decode().split('\0')[:-1]
assert set(staged) == {p.relative_to(root).as_posix() for p in paths}
assert not git('diff', '--', 'src/executor/xlang_vm/xlang_vm_loop.cpp', 'tests/run_fixtures.py', 'tests/run_fixtures.ps1').strip()
for p, h in control['unowned_tracked_dirty_sha256'].items():
    assert sha(root / p) == h
print('exact_doc_files_staged', len(paths), 'bytes', sum(p.stat().st_size for p in paths), 'manifest_sha256', sha(manifest), flush=True)
