"""Apply reviewed native cache only after the exact frozen CP3147 fixture passes."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
data = root / 'doc/performance/data'
scratch = root / 'scratch/performance'
proof_path = scratch / 'sqlite-statement-cache-r4-proposal-20261008-provenance.json'
assert sha(proof_path) == '1f99329a2ec92400a8e9b1f9e829ca3082f488db0ed02bf7952b20ac0740f201'
proof = json.loads(proof_path.read_text(encoding='utf-8'))
patch = scratch / 'sqlite-statement-cache-r4-proposal-20261008.patch'
assert sha(patch) == proof['patch_sha256'] == '87b300c1271c609172a51c110a2ec8ccdd13bcb2bbe4ada384f1c21039a802c5'
restoration_path = data / 'interpreter-lazy-globals-restoration-20261008.json'
restoration = json.loads(restoration_path.read_text(encoding='utf-8'))
assert restoration['status'] == 'accepted_source_and_live_release_restored'
sources = dict(restoration['restored_source_sha256'])
assert all(sha(root / name) == expected for name, expected in sources.items())
control = root / 'build-repro/controls/native-bound-zero-args-checkpoint-20261008'
manifest = json.loads((control / 'preserved-release-provenance.json').read_text(encoding='utf-8'))
release = root / 'build-repro/main-verify-20261006/Release'
assert manifest['accepted']
assert all(sha(control / name) == expected and sha(release / name) == expected
    for name, expected in manifest['files_sha256'].items())
reference_path = data / 'sqlite-statement-cache-r4-cpython3147-reference-20261008.json'
reference = json.loads(reference_path.read_text(encoding='utf-8'))
assert reference['status'] == 'terminal' and reference['exit_code'] == 0 and reference['output_matches_expected']
assert reference['cpython_version'].startswith('3.14.7 ')
cp_source = Path(reference['source'])
assert sha(cp_source) == reference['source_sha256']
for suffix, key in (('.stdout.log', 'stdout_sha256'), ('.stderr.log', 'stderr_sha256')):
    assert sha(reference_path.with_name(reference_path.stem + suffix)) == reference[key]
for row in proof['targets']:
    path = root / row['path']
    assert (sha(path) if path.exists() else None) == row['source_sha256_before'], row['path']
    candidate = root / row['candidate_copy']
    assert sha(candidate) == row['candidate_sha256'], row['path']
    if path.exists(): sources[row['path']] = sha(path)
    if row['path'] == 'tests/fixtures/core/sqlite_statement_cache.py':
        assert candidate.read_bytes().replace(b'\r\n', b'\n') == cp_source.read_bytes().replace(b'\r\n', b'\n')
before_path = scratch / 'sqlite-statement-cache-r4-actual-source-before-20261008'
inventory_path = data / 'sqlite-statement-cache-r4-final-source-20261008.json'
assert not before_path.exists() and not inventory_path.exists()
subprocess.run(['git', 'apply', '--check', str(patch)], check=True)
for name, expected in sources.items():
    target = before_path / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root / name, target)
    assert sha(target) == expected
(before_path / 'manifest.json').write_bytes((json.dumps(dict(source_sha256=sources,
    accepted_validation_sha256=restoration['accepted_validation_sha256'],
    accepted_release_manifest_sha256=sha(control / 'preserved-release-provenance.json'),
    added_paths=[row['path'] for row in proof['targets'] if row['source_sha256_before'] is None]),
    indent=2) + '\n').encode('utf-8'))
subprocess.run(['git', 'apply', str(patch)], check=True)
for row in proof['targets']:
    path = root / row['path']
    assert path.read_bytes().replace(b'\r\n', b'\n') == (root / row['candidate_copy']).read_bytes().replace(b'\r\n', b'\n'), row['path']
    sources[row['path']] = sha(path)
inventory_path.write_bytes((json.dumps(dict(status='applied_verified', source_sha256=sources,
    patch_sha256=sha(patch), proof_sha256=sha(proof_path),
    cpython_reference_sha256=sha(reference_path), accepted_raw_before_manifest_sha256=sha(before_path / 'manifest.json'),
    owned_sources=[row['path'] for row in proof['targets']]), indent=2) + '\n').encode('utf-8'))
print('Applied native cacheR4:', len(proof['targets']), 'owned targets;', len(sources), 'pinned combined sources')
