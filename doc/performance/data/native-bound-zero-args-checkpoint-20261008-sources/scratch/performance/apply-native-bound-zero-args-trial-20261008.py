"""Capture CP reference then apply the reviewed one-allocation callback trial."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
assert sys.version_info[:3]==(3,14,7)
root=Path.cwd()
base=root/'scratch/performance'
data=root/'doc/performance/data'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
control=root/'build-repro/controls/sqlite-identity-hash-checkpoint-20261008'
manifest=json.loads((control/'preserved-release-provenance.json').read_text(encoding='utf-8'))
assert manifest['accepted'] and manifest['commit']=='a09f84f547d082f51bf9e5d03bcc2a7c488978dd'
assert all(sha(control/p)==v for p,v in manifest['files_sha256'].items())
proof_path=base/'native-bound-zero-args-rebased-20261008-provenance.json'
assert sha(proof_path)=='7d46c6bb50a089008f1c85209fc8c2367f004cf634a1e893bbc70ff5185a37bc'
proof=json.loads(proof_path.read_text(encoding='utf-8'))
patch=base/'native-bound-zero-args-rebased-20261008.patch'
assert sha(patch)==proof['patch_sha256']=='bd0fefd6fa25011a4364b2310fdb2771bfd4b30de2f127c0d493c4b1f7830c98'
for row in proof['targets']:
    path=root/row['path']
    assert (sha(path) if path.exists() else None)==row['source_sha256_before']
    assert sha(root/row['candidate_copy'])==row['candidate_sha256']
fixture=base/'native-bound-zero-args-rebased-20261008-candidates/tests/fixtures/core/native_bound_zero_args.py'
expected=base/'native-bound-zero-args-rebased-20261008-candidates/tests/fixtures/expected/native_bound_zero_args.out'
prefix=data/'native-bound-zero-args-cpython3147-reference-20261008'
outputs=[Path(str(prefix)+s) for s in ('.json','.stdout.log','.stderr.log')]
assert not any(p.exists() for p in outputs)
env=os.environ.copy()
for name in ('PYTHONPATH','PYTHONIOENCODING','PYTHONPYCACHEPREFIX'): env.pop(name,None)
result=subprocess.run([sys.executable,'-u',str(fixture)],cwd=root,env=env,capture_output=True,timeout=90)
outputs[1].write_bytes(result.stdout)
outputs[2].write_bytes(result.stderr)
matched=result.stdout.decode('utf-8').replace('\r\n','\n').rstrip()==expected.read_text(encoding='utf-8').replace('\r\n','\n').rstrip()
outputs[0].write_bytes((json.dumps(dict(status='terminal',exit_code=result.returncode,output_matches_expected=matched,
    source=str(fixture),source_sha256=sha(fixture),expected_sha256=sha(expected),cpython_version=sys.version,
    stdout_sha256=sha(outputs[1]),stderr_sha256=sha(outputs[2])),indent=2)+'\n').encode('utf-8'))
assert result.returncode==0 and matched,(result.stdout+result.stderr).decode('utf-8')
print('CPython3.14.7 callback fixture passed',flush=True)
subprocess.run(['git','apply','--check',str(patch)],check=True)
subprocess.run(['git','apply',str(patch)],check=True)
files={}
for row in proof['targets']:
    path=root/row['path']
    assert path.read_bytes().replace(b'\r\n',b'\n')==(root/row['candidate_copy']).read_bytes().replace(b'\r\n',b'\n')
    files[row['path']]={'working_sha256':sha(path)}
output=data/'native-bound-zero-args-applied-source-20261008.json'
assert not output.exists()
output.write_bytes((json.dumps(dict(status='applied_verified',files=files,patch_sha256=sha(patch),
    accepted_control_commit=manifest['commit'],accepted_control_manifest_sha256=sha(control/'preserved-release-provenance.json')),indent=2)+'\n').encode('utf-8'))
print('Applied zero-argument bound callback trial:',len(files),'owned files',flush=True)
