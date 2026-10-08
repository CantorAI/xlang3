"""Apply the reviewed module-load loop proof; strict Python fixture is unchanged."""
import hashlib
import json
from pathlib import Path
import subprocess
root = Path.cwd()
base = root / 'scratch/performance'
data = root / 'doc/performance/data'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
proof_path = base / 'module-slot-loop-ownership-20261008-provenance.json'
assert sha(proof_path) == 'f92c2d988ed7c2759fe514596f86e5d0d2daab56b8017b30e6ce8e3b5786abad'
proof = json.loads(proof_path.read_text(encoding='utf-8'))
patch = Path(proof['patch'])
assert sha(patch) == proof['patch_sha256'] == 'b2440453fa4118e9d1481e202a60222cb72e107fe2d0543ea32697e682e174d6'
assert proof['actual_source_sha256_before'] == proof['actual_source_sha256_after']
assert all(sha(root / p)==value for p,value in proof['actual_source_sha256_before'].items())
assert all(sha(root / p)==value for p,value in proof['unchanged_frozen_sha256'].items())
assert all(sha(Path(proof['candidate_root']) / p)==value for p,value in proof['candidate_source_sha256'].items())
previous = data / 'sqlite-identity-hash-combined-final-source-20261008.json'
record = json.loads(previous.read_text(encoding='utf-8'))
assert all(sha(root / p)==r['working_sha256'] for p,r in record['files'].items())
output = data / 'sqlite-identity-hash-loop-proof-final-source-20261008.json'
assert not output.exists()
subprocess.run(['git','apply','--check',str(patch)],check=True)
subprocess.run(['git','apply',str(patch)],check=True)
for name in proof['candidate_source_sha256']:
    assert (root/name).read_bytes().replace(b'\r\n',b'\n') == (Path(proof['candidate_root'])/name).read_bytes().replace(b'\r\n',b'\n')
    record['files'][name] = {'working_sha256':sha(root/name)}
record.update(status='applied_verified_with_module_load_loop_proof', previous_inventory_sha256=sha(previous),
              module_slot_loop_patch_sha256=sha(patch), strict_python_fixture_unchanged=True)
output.write_bytes((json.dumps(record,indent=2)+'\n').encode('utf-8'))
print('Applied reviewed loop proof;', len(record['files']), 'compiled sources pinned', flush=True)
