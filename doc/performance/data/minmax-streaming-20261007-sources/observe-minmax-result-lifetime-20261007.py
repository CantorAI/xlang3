import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
probe = root / 'scratch/performance/minmax-result-lifetime-probe-20261007.py'
output = root / 'doc/performance/data/minmax-result-lifetime-observations-20261007.json'
assert not output.exists()
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
    env.pop(name, None)
observations = []
for name, runtime in (('cpython3147', Path(sys.executable)),
                      ('control', root / 'build-repro/controls/inherited-subscript-checkpoint-20261007/xlang3.exe'),
                      ('candidate', root / 'build-repro/main-verify-20261006/Release/xlang3.exe')):
    result = subprocess.run([str(runtime), str(probe)], cwd=root, env=env,
                            capture_output=True, timeout=60)
    assert result.returncode == 0, result.stderr
    observations.append({'runtime': name, 'result': json.loads(result.stdout),
                         'exe_sha256': hashlib.sha256(runtime.read_bytes()).hexdigest(),
                         'dll_sha256': hashlib.sha256(runtime.with_name('xlang3_runtime.dll').read_bytes()).hexdigest() if name != 'cpython3147' else None})
output.write_text(json.dumps({'probe_sha256': hashlib.sha256(probe.read_bytes()).hexdigest(),
                             'observations': observations}, indent=2) + '\n', encoding='utf-8')
for row in observations:
    print(row['runtime'], json.dumps(row['result']))
