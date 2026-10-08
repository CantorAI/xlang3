"""Stage exact owned files and verify raw evidence bytes in Git's index."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
run = lambda args: subprocess.check_output(['git', *args], cwd=root)
existing_staged = set(run(['diff', '--cached', '--name-only', '-z']).decode('utf-8').split('\0')[:-1])
archive = root / 'doc/performance/data/inherited-slot-proof-20261008-sources'
manifest_path = archive / 'manifest.json'
manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
sources = [entry['path'] for entry in json.loads((root / 'scratch/performance/inherited-slot-proof-proposal-r2-20261008-provenance.json').read_text(encoding='utf-8'))['targets']]
validation = json.loads((root / 'doc/performance/data/inherited-slot-proof-validation-20261008.json').read_text(encoding='utf-8'))
assert validation['status'] == 'validated'
assert all(digest(root / name) == validation['source_sha256'][name] for name in sources)
binary = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
assert validation['candidate_binary_sha256'] == {'exe': digest(binary), 'dll': digest(binary.with_name('xlang3_runtime.dll'))}
files = {'.gitattributes', *sources}
raw_hashes = {}
for name, entry in manifest['raw_files'].items():
    archived = archive / name
    assert archived.resolve().is_relative_to(archive.resolve())
    assert digest(archived) == entry['sha256'], name
    relative = archived.relative_to(root).as_posix()
    files.add(relative)
    raw_hashes[relative] = entry['sha256']
    original = Path(entry['source'])
    assert digest(original) == entry['sha256'], entry['source']
    if original.is_relative_to(root / 'doc/performance/data') and original.name.startswith(
            ('inherited-slot-proof', 'build-inherited-slot-proof', 'release-inherited-slot-proof', 'pyperformance-inherited-slot-proof')):
        relative = original.relative_to(root).as_posix()
        files.add(relative)
        raw_hashes[relative] = entry['sha256']
for name, sha in manifest['generated_files'].items():
    path = root / 'doc/performance' / name
    assert digest(path) == sha, name
    files.add(path.relative_to(root).as_posix())
    if path.is_relative_to(root / 'doc/performance/data'):
        raw_hashes[path.relative_to(root).as_posix()] = sha
files.add(manifest_path.relative_to(root).as_posix())
raw_hashes[manifest_path.relative_to(root).as_posix()] = digest(manifest_path)
pathspec = root / 'scratch/performance/inherited-slot-proof-owned-pathspec-20261008.bin'
assert existing_staged.issubset(files), 'Preserve unrelated staged work; do not stage this checkpoint yet'
pathspec_bytes = b''.join((':(literal)' + name).encode('utf-8') + b'\0' for name in sorted(files))
if pathspec.exists():
    assert pathspec.read_bytes() == pathspec_bytes
else:
    pathspec.write_bytes(pathspec_bytes)
# Nested archived scratch inputs match the repository's scratch ignore rule.
# Force only these verified literal paths; never discover or stage a directory.
run(['add', '-f', '--pathspec-from-file=' + str(pathspec), '--pathspec-file-nul'])
assert set(run(['diff', '--cached', '--name-only', '-z']).decode('utf-8').split('\0')[:-1]) == files
for name, sha in raw_hashes.items():
    assert hashlib.sha256(run(['show', ':' + name])).hexdigest() == sha, name
git_sources = {}
for name in sources:
    working, staged = (root / name).read_bytes(), run(['show', ':' + name])
    assert working.replace(b'\r\n', b'\n') == staged.replace(b'\r\n', b'\n'), name
    git_sources[name] = {'compiled_working_sha256': hashlib.sha256(working).hexdigest(),
                         'staged_sha256': hashlib.sha256(staged).hexdigest(),
                         'only_line_ending_normalization_allowed': True}
record_path = root / 'doc/performance/data/inherited-slot-proof-git-source-sha256-20261008.json'
assert not record_path.exists()
record_path.write_bytes((json.dumps(git_sources, indent=2) + '\n').encode('utf-8'))
run(['add', '--', record_path.relative_to(root).as_posix()])
files.add(record_path.relative_to(root).as_posix())
assert set(run(['diff', '--cached', '--name-only', '-z']).decode('utf-8').split('\0')[:-1]) == files
run(['diff', '--cached', '--check', '--', '.gitattributes', *sources,
     'doc/performance/inherited-slot-proof-checkpoint-20261008.md',
     'doc/performance/inherited-slot-proof-diagnostic-speed-20261008.svg',
     'doc/performance/inherited-slot-proof-official-speed-20261008.svg'])
print('Staged', len(files), 'exact owned paths; verified', len(raw_hashes), 'raw/generated data blobs')
