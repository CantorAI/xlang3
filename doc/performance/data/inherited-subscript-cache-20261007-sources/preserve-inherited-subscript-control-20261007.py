import hashlib
import json
from pathlib import Path
import shutil
import subprocess

root = Path.cwd()
validation = json.loads((root / 'doc/performance/data/dict-native-index-final-validation-20261007.json').read_text(encoding='utf-8'))
assert validation['status'] != 'running'
source = root / 'build-repro/main-verify-20261006/Release'
target = root / 'build-repro/controls/dict-intrinsic-index-checkpoint-20261007'
assert not target.exists()
assert hashlib.sha256((source / 'xlang3_runtime.dll').read_bytes()).hexdigest() == validation['candidate_binary_sha256']['dll']
old = root / 'build-repro/controls/dict-native-read-index-trial-20261007/preserved-release-provenance.json'
manifest = json.loads(old.read_text(encoding='utf-8'))
hashes = {}
for name in manifest['files_sha256']:
    src, dst = source / name, target / name
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    hashes[name] = hashlib.sha256(src.read_bytes()).hexdigest()
    assert hashes[name] == hashlib.sha256(dst.read_bytes()).hexdigest()
(target / 'preserved-release-provenance.json').write_text(json.dumps({'purpose': 'Validated dictionary checkpoint before inherited subscript cache changes; run path unchanged',
                                                                    'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                                                                    'files_sha256': hashes}, indent=2) + '\n', encoding='utf-8')
before = root / 'scratch/performance/inherited-subscript-cache-20261007-source-before'
before.mkdir()
for name in ('src/executor/xlang_vm/ops/xlang_vm_ops_containers.h', 'src/runtime/object_model.cpp'):
    shutil.copy2(root / name, before / Path(name).name)
print('Preserved and verified', len(hashes), 'Release files and two source snapshots')
