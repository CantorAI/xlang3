"""Root owns serial application and pins every compiled source byte."""
import hashlib
import json
from pathlib import Path
import subprocess

root = Path.cwd()
base = root / 'scratch/performance'
data = root / 'doc/performance/data'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
control = root / 'build-repro/controls/inherited-slot-proof-checkpoint-20261008'
manifest = json.loads((control / 'preserved-release-provenance.json').read_text(encoding='utf-8'))
assert manifest['accepted'] and all(sha(control / p) == v for p,v in manifest['files_sha256'].items())
proof_path = base / 'identity-last-use-r2-proposal-20261008-provenance.json'
assert sha(proof_path) == '82fc09bf044be8b5da015d97806d883a2fdb66efde69136f3a59c38e3e416279'
proof = json.loads(proof_path.read_text(encoding='utf-8'))
patch = Path(proof['patch'])
assert sha(patch) == proof['patch_sha256'] == '70cff5cc3dda8d2ed92fe0b5dfe28b868e9e7432071f831eb5de2bd4a7cd58e7'
assert proof['actual_source_sha256_before'] == proof['actual_source_sha256_after']
assert all(sha(root / p) == v for p,v in proof['actual_source_sha256_before'].items())
assert all(sha(Path(proof['candidate_root']) / p) == v for p,v in proof['candidate_source_sha256'].items())
cp = json.loads((data / 'identity-last-use-r2-cpython3147-reference-20261008.json').read_text(encoding='utf-8'))
assert cp['exit_code'] == 0 and cp['output_matches_expected'] and cp['groups'] == 7
assert cp['source_sha256'] == proof['candidate_source_sha256']['tests/fixtures/core/identity_last_use.py']
hash_patch = base / 'hash-exception-preservation-complete-proposed-20261008.patch'
assert sha(hash_patch) == 'b566a8ce3b7f53bb3ffdb7f5214825623d485a1848672c4bc8fda1e247089c34'
assert sha(root / 'src/builtins/functional_builtins.cpp') == 'de5920ff70a7a11069da2e5b5a27bfa9abbededf81a8a2e00e56cce584d1c979'
output = data / 'sqlite-identity-hash-combined-applied-source-20261008.json'
assert not output.exists()
subprocess.run(['git', 'apply', '--check', str(patch)], check=True)
subprocess.run(['git', 'apply', str(patch)], check=True)
for name in proof['candidate_source_sha256']:
    actual = (root / name).read_bytes().replace(b'\r\n', b'\n')
    proposed = (Path(proof['candidate_root']) / name).read_bytes().replace(b'\r\n', b'\n')
    assert actual == proposed, name
# The independent hash proposal predates the identity registration. Apply its
# unchanged engine/tests and merge only its two registration lines explicitly.
args = ['git', 'apply', '--exclude=tests/cpp/interpreter_tests.cpp']
subprocess.run(args + ['--check', str(hash_patch)], check=True)
subprocess.run(args + [str(hash_patch)], check=True)
interpreter = root / 'tests/cpp/interpreter_tests.cpp'
text = interpreter.read_text(encoding='utf-8')
anchor = '#include "identity_last_use_cases.h"\n'
assert text.count(anchor) == 1
text = text.replace(anchor, anchor + '#include "hash_exception_cases.h"\n')
anchor = '  xlang3::test::check_identity_last_use_cases(result);\n'
assert text.count(anchor) == 1
text = text.replace(anchor, anchor + '  xlang3::test::check_hash_exception_cases(result);\n')
interpreter.write_bytes(text.encode('utf-8'))
for name, anchor, addition in (
    ('tests/run_fixtures.py', 'CORE_CASES = """\n', 'hash_exception_preservation\n'),
    ('tests/run_fixtures.ps1', '$cases = @(\n', '    "identity_last_use",\n    "hash_exception_preservation",\n'),
):
    path = root / name
    text = path.read_text(encoding='utf-8')
    assert text.count(anchor) == 1
    path.write_bytes(text.replace(anchor, anchor + addition).encode('utf-8'))
old = json.loads((data / 'sqlite-aggregate-cursor-r6-applied-source-20261008.json').read_text(encoding='utf-8'))
names = set(old['files']) | set(proof['candidate_source_sha256']) | {
    'src/runtime/value_hash.cpp', 'src/builtins/functional_builtins.cpp',
    'tests/cpp/hash_exception_cases.h', 'tests/fixtures/core/hash_exception_preservation.py',
    'tests/fixtures/expected/hash_exception_preservation.out'}
record = dict(status='applied_verified', files={p: {'working_sha256': sha(root / p)} for p in sorted(names)},
              identity_patch_sha256=sha(patch), hash_exception_patch_sha256=sha(hash_patch),
              registration_merge='two hash CPP lines plus both permanent fixture lists',
              accepted_control_manifest_sha256=sha(control / 'preserved-release-provenance.json'))
output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))
print('Applied combined checkpoint:', len(names), 'owned source files', flush=True)
