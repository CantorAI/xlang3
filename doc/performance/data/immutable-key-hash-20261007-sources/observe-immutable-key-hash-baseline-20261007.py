"""Preserve the accepted Release and measure the next hypothesis serially."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
validation = json.loads((data / 'dict-missing-special-lookup-validation-20261007.json').read_text())
assert validation['status'] == 'validated'
release = root / 'build-repro/main-verify-20261006/Release'
control = root / 'build-repro/controls/dict-missing-special-lookup-checkpoint-20261007'
output = data / 'immutable-key-hash-baseline-20261007.json'
assert not control.exists() and not output.exists()
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert validation['candidate_binary_sha256'] == {'exe': digest(release / 'xlang3.exe'),
                                              'dll': digest(release / 'xlang3_runtime.dll')}
manifest = json.loads((data / 'dict-missing-special-lookup-preserved-control-20261007.json').read_text())
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
    'purpose': 'Accepted type-based missing lookup before immutable-key hash investigation; run path unchanged',
}, indent=2) + '\n', encoding='utf-8')
probe = root / 'scratch/performance/immutable-key-hash-probe-20261007.py'
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
    env.pop(name, None)
record = {'scope': 'Baseline diagnosis only, includes Python loop/assertion overhead; not official scores',
          'commit': commit, 'probe_sha256': digest(probe), 'observations': []}
for label, executable in (('cpython3147', Path(sys.executable)), ('xlang3', release / 'xlang3.exe')):
    result = subprocess.run([str(executable), str(probe)], cwd=root, env=env,
                            capture_output=True, timeout=120)
    entry = {'runtime': label, 'exit_code': result.returncode,
             'exe_sha256': digest(executable), 'stdout': result.stdout.decode('utf-8'),
             'stderr': result.stderr.decode('utf-8')}
    if label == 'xlang3':
        entry['dll_sha256'] = digest(executable.with_name('xlang3_runtime.dll'))
    if result.returncode == 0:
        entry['result'] = json.loads(entry['stdout'])
        assert len(entry['result']['rows']) == 8
        for row in entry['result']['rows']:
            assert len(row['samples_seconds']) == 5
            assert row['checked_hash_calls'] == [row['operations'] * row['hashes_per_key']] * 5
            print(label, row['shape'], row['bytes_width'], 'fresh', row['fresh_tuple'],
                  'hashes', row['hashes_per_key'], 'median_seconds', statistics.median(row['samples_seconds']), flush=True)
    record['observations'].append(entry)
record['status'] = 'terminal'
output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
assert all(row['exit_code'] == 0 for row in record['observations']), record
print('Preserved', len(hashes), 'Release files; retained 80 baseline hash samples')
