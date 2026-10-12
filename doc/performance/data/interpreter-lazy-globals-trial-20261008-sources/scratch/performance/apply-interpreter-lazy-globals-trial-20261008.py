"""Preserve the validated zero-argument control, then apply frozen lazy globals."""
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
validation_path = data / 'native-bound-zero-args-validation-20261008.json'
validation = json.loads(validation_path.read_text(encoding='utf-8'))
assert validation['terminal'] and validation['status'] == 'validated' and validation['hashes_unchanged']
assert validation['correctness_passed'] and validation['fixed_gate']['exit_code'] == 0
for name, expected in validation['source_sha256'].items():
    assert sha(root / name) == expected, name
for name, expected in validation['binaries_sha256'].items():
    assert sha(root / name) == expected, name
gate_path = data / validation['fixed_gate']['output']
assert sha(gate_path) == validation['fixed_gate']['sha256']
gate = json.loads(gate_path.read_text(encoding='utf-8'))
assert gate['status'] == 'pass' and len(gate['cases']) == 11
assert (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .1)
proof_path = scratch / 'interpreter-lazy-globals-zeroargc-actual-rebased-20261008-provenance.json'
assert sha(proof_path) == 'd3d7f6ee8c0e67b752eaa70fc2dc0023666475b38f8fc334c91a6a1943f3df21'
proof = json.loads(proof_path.read_text(encoding='utf-8'))
patch = Path(proof['patch'])
assert sha(patch) == proof['patch_sha256'] == '353afa8c500b6ec142af0f8b5944e0582ed3cabef860dd038c29b71f18d56331'
for name, expected in proof['actual_source_sha256_before'].items():
    assert sha(root / name) == expected, name
candidate_root = Path(proof['candidate_root'])
for name, expected in proof['candidate_source_sha256'].items():
    assert sha(candidate_root / name) == expected, name
assert not (root / 'tests/cpp/interpreter_lazy_globals_cases.h').exists()
output = data / 'interpreter-lazy-globals-final-source-20261008.json'
assert not output.exists()
subprocess.run(['git', 'apply', '--check', str(patch)], check=True)

release = root / 'build-repro/main-verify-20261006/Release'
target = root / 'build-repro/controls/native-bound-zero-args-checkpoint-20261008'
assert not target.exists()
previous = json.loads((root / 'build-repro/controls/sqlite-identity-hash-checkpoint-20261008/preserved-release-provenance.json').read_text(encoding='utf-8'))
files = {}
for name in previous['files_sha256']:
    original = release / name
    copied = target / name
    copied.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(original, copied)
    files[name] = sha(original)
    assert sha(copied) == files[name]
(target / 'preserved-release-provenance.json').write_bytes((json.dumps(dict(
    accepted=True, commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
    purpose='Validated neutral zero-argument callback control before isolated lazy-globals trial',
    validation_sha256=sha(validation_path), source_sha256=validation['source_sha256'],
    files_sha256=files), indent=2) + '\n').encode('utf-8'))
print('Preserved validated zero-argument control:', len(files), 'Release files', flush=True)

subprocess.run(['git', 'apply', str(patch)], check=True)
sources = dict(validation['source_sha256'])
for name in proof['candidate_source_sha256']:
    path = root / name
    assert path.read_bytes().replace(b'\r\n', b'\n') == (candidate_root / name).read_bytes().replace(b'\r\n', b'\n'), name
    sources[name] = sha(path)
output.write_bytes((json.dumps(dict(status='applied_verified', source_sha256=sources,
    patch_sha256=sha(patch), prior_validation_sha256=sha(validation_path),
    control_manifest_sha256=sha(target / 'preserved-release-provenance.json'),
    owned_sources=list(proof['candidate_source_sha256'])), indent=2) + '\n').encode('utf-8'))
print('Applied lazy-globals trial:', len(proof['candidate_source_sha256']), 'owned files;', len(sources), 'combined source hashes', flush=True)
