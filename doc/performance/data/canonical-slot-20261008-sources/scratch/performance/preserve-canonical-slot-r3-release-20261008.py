"""Preserve the rejected R3 Release before rebuilding the fixed candidate path."""
import hashlib
import json
from pathlib import Path
import shutil
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
source = root / 'build-repro/main-verify-20261006/Release'
target = root / 'build-repro/controls/canonical-slot-r3-trial-20261008'
assert not target.exists()
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
early = json.loads((root / 'doc/performance/data/canonical-slot-early-r2-20261008.json').read_text(encoding='utf-8'))
assert early['status'] == 'terminal'
assert early['candidate_binary_sha256'] == {'exe': digest(source / 'xlang3.exe'), 'dll': digest(source / 'xlang3_runtime.dll')}
assert all(digest(root / name) == sha for name, sha in early['source_sha256'].items())
manifest = json.loads((root / 'build-repro/controls/vm-captured-lookup-checkpoint-20261007/preserved-release-provenance.json').read_text(encoding='utf-8'))
hashes = {}
for name in manifest['files_sha256']:
    src, dst = source / name, target / name
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    hashes[name] = digest(src)
    assert digest(dst) == hashes[name]
record = {'status': 'rejected_inherited_slot_regression', 'accepted': False,
          'purpose': 'Reproduce R3 rejected diagnostics; never use as accepted baseline',
          'source_sha256': early['source_sha256'], 'files_sha256': hashes}
(target / 'preserved-release-provenance.json').write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))
print('Preserved rejected R3:', len(hashes), 'Release files')
