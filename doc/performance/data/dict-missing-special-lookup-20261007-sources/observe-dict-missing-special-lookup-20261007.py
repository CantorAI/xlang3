"""Preserve the accepted Release; record differential protocol observations."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
validation = json.loads((data / 'native-trivial-callback-validation-20261007.json').read_text())
assert validation['status'] == 'validated'
release = root / 'build-repro/main-verify-20261006/Release'
control = root / 'build-repro/controls/native-trivial-callback-checkpoint-20261007'
output = data / 'dict-missing-special-lookup-before-20261007.json'
assert not control.exists() and not output.exists()
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert validation['candidate_binary_sha256'] == {
    'exe': digest(release / 'xlang3.exe'),
    'dll': digest(release / 'xlang3_runtime.dll'),
}
manifest = json.loads((data / 'native-trivial-callback-preserved-control-20261007.json').read_text())
hashes = {}
for name in manifest['files_sha256']:
    source, target = release / name, control / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    hashes[name] = digest(source)
    assert digest(target) == hashes[name]
commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
(control / 'preserved-release-provenance.json').write_text(json.dumps({
    'commit': commit, 'files_sha256': hashes,
    'purpose': 'Accepted native trivial callback checkpoint before dict special lookup changes; run path unchanged',
}, indent=2) + '\n', encoding='utf-8')
probe = root / 'scratch/performance/dict-missing-special-lookup-probe-20261007.py'
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
    env.pop(name, None)
record = {'scope': 'Correctness differential, not timing', 'commit': commit,
          'probe_sha256': digest(probe), 'observations': []}
for label, executable in (('cpython3147', Path(sys.executable)), ('xlang3', release / 'xlang3.exe')):
    result = subprocess.run([str(executable), str(probe)], cwd=root, env=env,
                            capture_output=True, timeout=30)
    entry = {'runtime': label, 'exit_code': result.returncode,
             'exe_sha256': digest(executable), 'stdout': result.stdout.decode('utf-8'),
             'stderr': result.stderr.decode('utf-8')}
    if result.returncode == 0:
        entry['results'] = [json.loads(line) for line in entry['stdout'].splitlines()]
    record['observations'].append(entry)
    print(label, entry['stdout'], entry['stderr'], flush=True)
record['status'] = 'terminal'
output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
assert all(row['exit_code'] == 0 for row in record['observations'])
print('Preserved', len(hashes), 'Release files; retained both observations')
