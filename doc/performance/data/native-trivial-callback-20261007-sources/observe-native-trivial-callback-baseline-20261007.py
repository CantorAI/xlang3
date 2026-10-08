import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
probe = root / 'scratch/performance/native-trivial-callback-probe-20261007.py'
output = root / 'doc/performance/data/native-trivial-callback-baseline-20261007.json'
assert not output.exists()
runtime = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
    env.pop(name, None)
record = {'scope': 'Diagnostic baseline only, distinct loop shapes; not official benchmark scores',
          'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
          'probe_sha256': digest(probe), 'observations': []}
for name, executable in (('cpython3147', Path(sys.executable)), ('xlang3', runtime)):
    result = subprocess.run([str(executable), str(probe)], cwd=root, env=env,
                            capture_output=True, timeout=60)
    assert result.returncode == 0, result.stderr
    observed = json.loads(result.stdout)
    record['observations'].append({'runtime': name, 'exe_sha256': digest(executable),
                                   'dll_sha256': digest(runtime.with_name('xlang3_runtime.dll')) if name == 'xlang3' else None,
                                   'result': observed})
    for row in observed['rows']:
        print(name, row['path'], 'median_seconds', statistics.median(row['samples_seconds']))
record['status'] = 'terminal'
output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
