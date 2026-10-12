"""Preserve accepted Release and collect the new fixture's pre-change behavior."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
record = json.loads((root / 'doc/performance/data/inherited-subscript-cache-validation-20261007.json').read_text())
assert record['status'] == 'validated'
source = root / 'build-repro/main-verify-20261006/Release'
assert record['candidate_binary_sha256'] == {'exe': digest(source / 'xlang3.exe'), 'dll': digest(source / 'xlang3_runtime.dll')}
target = root / 'build-repro/controls/inherited-subscript-checkpoint-20261007'
assert not target.exists()
manifest = json.loads((root / 'build-repro/controls/dict-intrinsic-index-checkpoint-20261007/preserved-release-provenance.json').read_text())
hashes = {}
for name in manifest['files_sha256']:
    src, dst = source / name, target / name
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    hashes[name] = digest(src)
    assert digest(dst) == hashes[name]
provenance = {'purpose': 'Accepted inherited-subscript Release before native min/max streaming; run path unchanged',
              'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
              'files_sha256': hashes}
(target / 'preserved-release-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
before = root / 'scratch/performance/minmax-streaming-20261007-source-before'
before.mkdir()
for name in ('src/builtins/functional_builtins.cpp', 'tests/run_fixtures.py'):
    shutil.copy2(root / name, before / Path(name).name)
probe = root / 'scratch/performance/minmax-streaming-fixture-next-20261007.py'
output = root / 'doc/performance/data/minmax-streaming-fixture-before-20261007.json'
assert not output.exists()
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
    env.pop(name, None)
observations = []
for name, executable in (('cpython3147', Path(sys.executable)), ('candidate', source / 'xlang3.exe')):
    result = subprocess.run([str(executable), str(probe)], cwd=root, env=env,
                            capture_output=True, timeout=60)
    observations.append({'runtime': name, 'exit_code': result.returncode,
                         'stdout': result.stdout.decode('utf-8', errors='replace'),
                         'stderr': result.stderr.decode('utf-8', errors='replace')})
assert observations[0]['exit_code'] == 0, observations[0]
assert observations[1]['exit_code'] != 0, 'Revalidate original defect before changing engine'
output.write_text(json.dumps({'probe_sha256': digest(probe),
                             'control_binary_sha256': record['candidate_binary_sha256'],
                             'observations': observations}, indent=2) + '\n', encoding='utf-8')
expected = before / 'minmax_streaming.out'
expected.write_bytes(observations[0]['stdout'].encode('utf-8').replace(b'\r\n', b'\n'))
print('Preserved', len(hashes), 'Release files; CPython fixture passed and current candidate defect reproduced')
