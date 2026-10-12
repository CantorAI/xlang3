"""Retain the corrected candidate results and compare with the saved CPython run."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

root = Path.cwd()
data = root / 'doc/performance/data'
before = json.loads((data / 'dict-missing-special-lookup-before-20261007.json').read_text())
probe = root / 'scratch/performance/dict-missing-special-lookup-probe-20261007.py'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert digest(probe) == before['probe_sha256']
output = data / 'dict-missing-special-lookup-after-20261007.json'
assert not output.exists()
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONIOENCODING', 'PYTHONPATH', 'PYTHONPYCACHEPREFIX'):
    env.pop(name, None)
result = subprocess.run([str(candidate), str(probe)], cwd=root, env=env,
                        capture_output=True, timeout=30)
observed = [json.loads(line) for line in result.stdout.decode('utf-8').splitlines()] if result.returncode == 0 else []
expected = before['observations'][0]['results']
record = {'exit_code': result.returncode, 'probe_sha256': digest(probe),
          'binary_sha256': {'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))},
          'stdout': result.stdout.decode('utf-8'), 'stderr': result.stderr.decode('utf-8'),
          'results': observed, 'matches_cpython3147': observed == expected}
output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
assert result.returncode == 0 and observed == expected, record
print('All 13 protocol observations match the preserved CPython 3.14.7 results')
