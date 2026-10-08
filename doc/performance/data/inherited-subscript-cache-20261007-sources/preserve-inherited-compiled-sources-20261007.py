"""Preserve compiled source bytes and document Git newline normalization."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
archive = data / 'inherited-subscript-cache-20261007-sources'
compiled = archive / 'compiled-sources'
assert not compiled.exists()
compiled.mkdir()
digest = lambda raw: hashlib.sha256(raw).hexdigest()
git = lambda *args: subprocess.check_output(['git', *args], cwd=root)
record_path = data / 'inherited-subscript-cache-validation-20261007.json'
record = json.loads(record_path.read_text())
manifest_path = archive / 'manifest.json'
manifest = json.loads(manifest_path.read_text())
inventory_path = data / 'inherited-subscript-cache-evidence-20261007.json'
inventory = json.loads(inventory_path.read_text())
record['repository_source_sha256'] = {}
new_paths = []
for name, sha in record['source_sha256'].items():
    raw = (root / name).read_bytes()
    assert digest(raw) == sha
    staged = git('show', ':' + name)
    assert staged == raw.replace(b'\r\n', b'\n'), name
    record['repository_source_sha256'][name] = digest(staged)
    target = compiled / Path(name).name
    assert not target.exists()
    target.write_bytes(raw)
    manifest['files'][str(target.relative_to(archive)).replace('\\', '/')] = sha
    new_paths.append(target)
record['source_newline_note'] = 'source_sha256 hashes actual compiler inputs, archived byte-exact; repository_source_sha256 hashes Git LF-normalized equivalents'
record_path.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
report = root / 'doc/performance/inherited-subscript-cache-checkpoint-20261007.md'
raw = report.read_bytes().replace(b'\r\n', b'\n')
raw += b'\nThe source archive also contains byte-exact compiler inputs in `compiled-sources`.\nThe validation record distinguishes their hashes from Git LF-normalized source\nhashes; the source text is identical after newline normalization.\n'
report.write_bytes(raw)
source = Path(__file__)
target = archive / source.name
target.write_bytes(source.read_bytes())
manifest['files'][target.name] = digest(target.read_bytes())
new_paths.append(target)
manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
for path in [*new_paths, record_path, manifest_path, report]:
    inventory['files'][str(path.relative_to(root)).replace('\\', '/')] = digest(path.read_bytes())
inventory_path.write_text(json.dumps(inventory, indent=2) + '\n', encoding='utf-8')
owned = sorted(set([*record['source_sha256'], '.gitattributes', *inventory['files'],
                    str(inventory_path.relative_to(root)).replace('\\', '/')]))
assert set(git('diff', '--cached', '--name-only', '-z').decode().rstrip('\0').split('\0')) <= set(owned)
subprocess.run(['git', 'add', '--', *owned], cwd=root, check=True)
assert set(git('diff', '--cached', '--name-only', '-z').decode().rstrip('\0').split('\0')) == set(owned)
for name, sha in inventory['files'].items():
    assert digest((root / name).read_bytes()) == sha
    assert git('show', ':' + name) == (root / name).read_bytes(), name
for name, sha in record['repository_source_sha256'].items():
    assert digest(git('show', ':' + name)) == sha
subprocess.run(['git', 'diff', '--cached', '--check', '--', '.gitattributes',
                *record['source_sha256'], str(report.relative_to(root))], cwd=root, check=True)
print('Preserved exact compiler inputs and verified', len(owned), 'owned staged files')
