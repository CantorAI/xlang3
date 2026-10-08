"""Preserve validated Release before ordinary-call trial; fixed run path remains."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
record = json.loads((root / 'doc/performance/data/captured-lookup-validation-r2-20261007.json').read_text(encoding='utf-8'))
assert record['status'] == 'validated'
source = root / 'build-repro/main-verify-20261006/Release'
target = root / 'build-repro/controls/captured-lookup-checkpoint-20261007'
assert not target.exists()
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert record['candidate_binary_sha256'] == {'exe': digest(source / 'xlang3.exe'), 'dll': digest(source / 'xlang3_runtime.dll')}
manifest = json.loads((root / 'doc/performance/data/captured-lookup-preserved-control-20261007.json').read_text(encoding='utf-8'))
hashes = {}
for name in manifest['files_sha256']:
    src, dst = source / name, target / name
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    hashes[name] = digest(src)
    assert digest(dst) == hashes[name]
provenance = {'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
              'purpose': 'Accepted captured lookup Release before ordinary VM call trial', 'files_sha256': hashes}
(target / 'preserved-release-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
print('Preserved', len(hashes), 'Release files')
