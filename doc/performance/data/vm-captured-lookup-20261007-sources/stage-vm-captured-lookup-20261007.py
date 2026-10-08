"""Stage exact terminal source/evidence inventory; preserve unrelated work."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
git = lambda *args: subprocess.check_output(['git', *args], cwd=root)
assert git('branch', '--show-current').strip() == b'main'
assert not git('diff', '--cached', '--name-only').strip(), 'Preserve unrelated staging'
record = json.loads((data / 'vm-captured-lookup-validation-20261007.json').read_text(encoding='utf-8'))
assert record['status'] in ('validated', 'correctness_and_gate_passed_official_failed')
inventory_path = data / 'vm-captured-lookup-evidence-20261007.json'
inventory = json.loads(inventory_path.read_text(encoding='utf-8'))
assert inventory['status'] == 'terminal' and inventory['raw_sample_count'] == 770 and inventory['summary_row_count'] == 11
digest = lambda raw: hashlib.sha256(raw).hexdigest()
for name, sha in inventory['files'].items():
    assert digest((root / name).read_bytes()) == sha, name
for name, sha in record['source_sha256'].items():
    assert digest((root / name).read_bytes()) == sha, name
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
assert record['candidate_binary_sha256'] == {'exe': digest(candidate.read_bytes()),
                                         'dll': digest(candidate.with_name('xlang3_runtime.dll').read_bytes())}
for phase in record['phases']:
    assert digest((data / phase['log']).read_bytes()) == phase['sha256']
report = root / 'doc/performance/vm-captured-lookup-checkpoint-20261007.md'
for target in re.findall(r'\]\(([^)]+)\)', report.read_text(encoding='utf-8')):
    assert (report.parent / target).exists(), target
authored = ['.gitattributes', *record['source_sha256'], report.relative_to(root).as_posix()]
subprocess.run(['git', 'diff', '--check', '--', *authored], cwd=root, check=True)
owned = sorted(set([*authored, *inventory['files'], inventory_path.relative_to(root).as_posix()]))
subprocess.run(['git', 'add', '--', *owned], cwd=root, check=True)
assert set(git('diff', '--cached', '--name-only', '-z').decode().rstrip('\0').split('\0')) == set(owned)
for name in inventory['files']:
    assert git('show', ':' + name) == (root / name).read_bytes(), name
source_info = json.loads((data / 'vm-captured-lookup-source-provenance-20261007.json').read_text(encoding='utf-8'))
for name, sha in source_info['repository_lf_sha256'].items():
    assert digest(git('show', ':' + name)) == sha, name
subprocess.run(['git', 'diff', '--cached', '--check', '--', *authored], cwd=root, check=True)
print('Staged and byte-verified', len(owned), 'owned files')
