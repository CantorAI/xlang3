"""Stage only verified, owned files after terminal benchmark validation."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
git = lambda *args: subprocess.check_output(['git', *args], cwd=root)
assert git('branch', '--show-current').strip() == b'main'
assert git('diff', '--cached', '--name-only').strip() == b'', 'Preserve unrelated staging'
data = root / 'doc/performance/data'
record = json.loads((data / 'inherited-subscript-cache-validation-20261007.json').read_text())
assert record['status'] in ('validated', 'correctness_and_gate_passed_official_failed')
inventory_path = data / 'inherited-subscript-cache-evidence-20261007.json'
inventory = json.loads(inventory_path.read_text())
assert inventory['status'] == 'terminal' and inventory['raw_sample_count'] == 980
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
for name, sha in inventory['files'].items():
    assert digest(root / name) == sha, name
for name, sha in record['source_sha256'].items():
    assert digest(root / name) == sha, name
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
assert record['candidate_binary_sha256'] == {
    'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))}
report = root / 'doc/performance/inherited-subscript-cache-checkpoint-20261007.md'
for relative in re.findall(r'\]\(([^)]+)\)', report.read_text()):
    if not relative.startswith('https:'):
        assert (report.parent / relative).exists(), relative
sources = sorted(record['source_sha256'])
authored = ['.gitattributes', *sources, str(report.relative_to(root)).replace('\\', '/')]
subprocess.run(['git', 'diff', '--check', '--', *authored], cwd=root, check=True)
owned = sorted(set([*authored, *inventory['files'], str(inventory_path.relative_to(root)).replace('\\', '/')]))
subprocess.run(['git', 'add', '--', *owned], cwd=root, check=True)
staged = git('diff', '--cached', '--name-only', '-z').decode().rstrip('\0').split('\0')
assert set(staged) == set(owned), (set(staged) - set(owned), set(owned) - set(staged))
for name in inventory['files']:
    if name.startswith('doc/performance/data/'):
        assert git('show', ':' + name) == (root / name).read_bytes(), name
subprocess.run(['git', 'diff', '--cached', '--check', '--', *authored], cwd=root, check=True)
print('Staged and byte-verified', len(staged), 'owned files')
