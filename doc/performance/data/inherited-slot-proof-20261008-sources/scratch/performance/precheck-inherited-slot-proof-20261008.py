"""Verify frozen proposal inputs and its CPython fixture before engine edits."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
scratch = root / 'scratch/performance'
manifest = json.loads((scratch / 'inherited-slot-proof-proposal-r2-20261008-provenance.json').read_text(encoding='utf-8'))
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip() == manifest['head']
for entry in manifest['targets']:
    assert digest(root / entry['path']) == entry['source_sha256_before'] == entry['source_sha256_after']
    assert digest(root / entry['candidate_copy']) == entry['candidate_sha256']
patch = scratch / 'inherited-slot-proof-proposal-r2-20261008.patch'
assert digest(patch) == '1452f7291f98998e765ab764010e8a5f0fc7f6038ee30b5fc5a090ee63e53e71'
control = root / 'build-repro/controls/canonical-slot-checkpoint-20261008'
preserved = json.loads((control / 'preserved-release-provenance.json').read_text(encoding='utf-8'))
assert preserved['accepted'] and preserved['commit'] == manifest['head']
for name, sha in preserved['files_sha256'].items():
    assert digest(control / name) == sha
candidate_tree = scratch / 'inherited-slot-proof-proposal-r2-20261008-candidates'
fixture = candidate_tree / 'tests/fixtures/core/canonical_slot_reads.py'
gold = (candidate_tree / 'tests/fixtures/expected/canonical_slot_reads.out').read_text(encoding='utf-8').replace('\r\n', '\n').strip()
env = os.environ.copy()
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
    env.pop(name, None)
completed = subprocess.run([sys.executable, str(fixture)], cwd=root, env=env, capture_output=True, timeout=90)
prefix = root / 'doc/performance/data/inherited-slot-proof-cpython3147-20261008'
stdout, stderr, output = (prefix.with_name(prefix.name + suffix) for suffix in ('-stdout.log', '-stderr.log', '.json'))
assert not any(path.exists() for path in (stdout, stderr, output))
stdout.write_bytes(completed.stdout)
stderr.write_bytes(completed.stderr)
matches = completed.stdout.decode('utf-8').replace('\r\n', '\n').strip() == gold
record = {'status': 'terminal' if completed.returncode == 0 and matches else 'failed_fixture',
          'scope': 'CPython correctness precheck; no benchmark score', 'exit_code': completed.returncode,
          'expected_output_matches': matches, 'fixture_sha256': digest(fixture),
          'stdout_sha256': digest(stdout), 'stderr_sha256': digest(stderr), 'source_hashes_verified': True,
          'patch_sha256': digest(patch), 'accepted_control_files_verified': len(preserved['files_sha256'])}
output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))
print('CPython 3.14.7 proposal fixture:', record['status'], flush=True)
assert completed.returncode == 0 and matches, completed.stdout + completed.stderr
subprocess.run(['git', 'apply', '--check', '--', str(patch)], cwd=root, check=True)
print('11 source hashes, 140 control files and patch applicability verified')
