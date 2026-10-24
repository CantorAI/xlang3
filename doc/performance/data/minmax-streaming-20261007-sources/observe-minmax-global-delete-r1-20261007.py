import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
probe = root / 'scratch/performance/minmax-result-lifetime-probe-20261007.py'
reference = root / 'doc/performance/data/minmax-result-lifetime-observations-20261007.json'
output = root / 'doc/performance/data/minmax-global-delete-r1-lifetime-observations-20261007.json'
assert not output.exists()
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
baseline = json.loads(reference.read_text())
assert baseline['probe_sha256'] == digest(probe)
runtime = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
    env.pop(name, None)
result = subprocess.run([str(runtime), str(probe)], cwd=root, env=env, capture_output=True, timeout=60)
assert result.returncode == 0, result.stderr
actual = json.loads(result.stdout)
expected = baseline['observations'][0]['result']
output.write_text(json.dumps({'reference_sha256': digest(reference), 'probe_sha256': digest(probe),
                             'candidate_dll_sha256': digest(runtime.with_name('xlang3_runtime.dll')),
                             'candidate': actual, 'cpython3147': expected,
                             'matches_cpython': actual == expected}, indent=2) + '\n', encoding='utf-8')
print(json.dumps(actual))
print('Matches CPython:', actual == expected)
