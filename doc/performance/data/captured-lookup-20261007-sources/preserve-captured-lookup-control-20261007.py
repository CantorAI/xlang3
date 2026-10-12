"""Preserve accepted immutable-key hash Release before the next engine trial."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

root = Path.cwd()
data = root / 'doc/performance/data'
record = json.loads((data / 'immutable-key-hash-validation-20261007.json').read_text())
assert record['status'] == 'validated'
source = root / 'build-repro/main-verify-20261006/Release'
target = root / 'build-repro/controls/immutable-key-hash-checkpoint-20261007'
assert not target.exists()
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert record['candidate_binary_sha256'] == {'exe': digest(source / 'xlang3.exe'), 'dll': digest(source / 'xlang3_runtime.dll')}
manifest = json.loads((data / 'immutable-key-hash-preserved-control-20261007.json').read_text())
hashes = {}
for name in manifest['files_sha256']:
    src, dst = source / name, target / name
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    hashes[name] = digest(src)
    assert digest(dst) == hashes[name]
(target / 'preserved-release-provenance.json').write_text(json.dumps({
    'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
    'purpose': 'Accepted immutable-key hash checkpoint before captured lookup trial; run path unchanged',
    'files_sha256': hashes}, indent=2) + '\n', encoding='utf-8')
print('Preserved', len(hashes), 'Release files')
