"""Stage only verified terminal source/evidence; preserve unrelated work."""
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
record = json.loads((data / 'minmax-streaming-validation-20261007.json').read_text())
assert record['status'] in ('validated', 'correctness_and_gate_passed_official_failed')
inventory_path = data / 'minmax-streaming-evidence-20261007.json'
inventory = json.loads(inventory_path.read_text())
assert inventory['status'] == 'terminal' and inventory['raw_sample_count'] == 420
digest = lambda raw: hashlib.sha256(raw).hexdigest()
for name, sha in inventory['files'].items():
    assert digest((root / name).read_bytes()) == sha, name
for name, sha in record['source_sha256'].items():
    assert digest((root / name).read_bytes()) == sha, name
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
assert record['candidate_binary_sha256'] == {'exe': digest(candidate.read_bytes()),
                                         'dll': digest(candidate.with_name('xlang3_runtime.dll').read_bytes())}
report = root / 'doc/performance/minmax-streaming-checkpoint-20261007.md'
for target in re.findall(r'\]\(([^)]+)\)', report.read_text()):
    if not target.startswith('https:'):
        assert (report.parent / target).exists(), target
authored = ['.gitattributes', *record['source_sha256'], str(report.relative_to(root)).replace('\\', '/')]
subprocess.run(['git', 'diff', '--check', '--', *authored], cwd=root, check=True)
owned = sorted(set([*authored, *inventory['files'], str(inventory_path.relative_to(root)).replace('\\', '/')]))
subprocess.run(['git', 'add', '--', *owned], cwd=root, check=True)
assert set(git('diff', '--cached', '--name-only', '-z').decode().rstrip('\0').split('\0')) == set(owned)
for name in inventory['files']:
    assert git('show', ':' + name) == (root / name).read_bytes(), name
source_info = json.loads((data / 'minmax-streaming-source-provenance-20261007.json').read_text())
for name, sha in source_info['repository_lf_sha256'].items():
    assert digest(git('show', ':' + name)) == sha, name
subprocess.run(['git', 'diff', '--cached', '--check', '--', *authored], cwd=root, check=True)
print('Staged and byte-verified', len(owned), 'owned files')
