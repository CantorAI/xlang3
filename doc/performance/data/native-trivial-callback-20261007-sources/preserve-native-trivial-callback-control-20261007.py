import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
record = json.loads((root / 'doc/performance/data/minmax-streaming-validation-20261007.json').read_text())
assert record['status'] == 'validated'
source = root / 'build-repro/main-verify-20261006/Release'
target = root / 'build-repro/controls/minmax-streaming-checkpoint-20261007'
assert not target.exists()
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert record['candidate_binary_sha256'] == {'exe': digest(source / 'xlang3.exe'), 'dll': digest(source / 'xlang3_runtime.dll')}
manifest = json.loads((root / 'build-repro/controls/inherited-subscript-checkpoint-20261007/preserved-release-provenance.json').read_text())
hashes = {}
for name in manifest['files_sha256']:
    src, dst = source / name, target / name
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    hashes[name] = digest(src)
    assert digest(dst) == hashes[name]
(target / 'preserved-release-provenance.json').write_text(json.dumps({
    'purpose': 'Validated min/max and global lifetime checkpoint before native trivial-function optimization; run path unchanged',
    'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
    'files_sha256': hashes}, indent=2) + '\n', encoding='utf-8')
before = root / 'scratch/performance/native-trivial-callback-20261007-source-before'
before.mkdir()
for name in ('src/runtime/functional_iterators.cpp', 'src/executor/xlang_vm/xlang_vm_inline_call.h',
             'src/executor/xlang_vm/ops/xlang_vm_ops_call.h', 'tests/run_fixtures.py'):
    shutil.copy2(root / name, before / Path(name).name)
print('Preserved and byte-verified', len(hashes), 'Release files and four source inputs')
