"""Publish only the reviewed frozen preview; never discover new scratch inputs."""
import hashlib
import json
from pathlib import Path
import shutil
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
preview = root / 'scratch/performance/inherited-slot-proof-report-preview-20261008'
target = root / 'doc/performance'
archive_name = 'data/inherited-slot-proof-20261008-sources'
manifest_path = preview / archive_name / 'manifest.json'
manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
assert manifest['status'] == 'terminal evidence archive'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
files = dict(manifest['generated_files'])
for name, entry in manifest['raw_files'].items():
    files[archive_name + '/' + name] = entry['sha256']
    assert digest(Path(entry['source'])) == entry['sha256'], entry['source']
files[archive_name + '/manifest.json'] = digest(manifest_path)
for name, sha in files.items():
    src, dst = preview / name, target / name
    assert src.resolve().is_relative_to(preview.resolve())
    assert dst.resolve().is_relative_to(target.resolve())
    assert digest(src) == sha, name
    if dst.exists():
        assert digest(dst) == sha, name
    else:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    assert digest(dst) == sha, name
print('Published', len(files), 'verified files from frozen preview')
