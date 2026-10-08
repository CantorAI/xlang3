import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
probe = root / 'scratch/performance/dict-intrinsic-overwrite-reentry-20261007.py'
out = root / 'doc/performance/data/dict-intrinsic-overwrite-reentry-before-20261007.json'
assert not out.exists()
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
rows = []
for label, runtime in (('cpython3147', Path(sys.executable)),
                       ('candidate', root / 'build-repro/main-verify-20261006/Release/xlang3.exe'),
                       ('accepted_control', root / 'build-repro/controls/live-eval-checkpoint-20261007/xlang3.exe')):
    result = subprocess.run([str(runtime), str(probe)], env=env, capture_output=True, timeout=30)
    rows.append({'runtime': label, 'executable': str(runtime), 'exit_code': result.returncode,
                 'stdout': result.stdout.decode('utf-8', errors='replace'),
                 'stderr': result.stderr.decode('utf-8', errors='replace'),
                 'exe_sha256': hashlib.sha256(runtime.read_bytes()).hexdigest(),
                 'dll_sha256': hashlib.sha256(runtime.with_name('xlang3_runtime.dll').read_bytes()).hexdigest()
                     if label != 'cpython3147' else None})
    print(label, result.returncode, flush=True)
out.write_text(json.dumps({'probe_sha256': hashlib.sha256(probe.read_bytes()).hexdigest(),
                           'observations': rows}, indent=2) + '\n', encoding='utf-8')
