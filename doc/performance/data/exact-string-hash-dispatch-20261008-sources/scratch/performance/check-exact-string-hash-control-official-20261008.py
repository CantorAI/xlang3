"""Fresh unchanged official SQLGlot parse reference on preserved accepted Release."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
data = root / 'doc/performance/data'
prefix = 'exact-string-hash-control-sqlglot-20261008'
receipt = data / (prefix + '-receipt.json')
output = data / (prefix + '.json')
log = data / (prefix + '.log')
assert not any(p.exists() for p in (receipt, output, log))
exe = root / 'build-repro/controls/inherited-slot-proof-checkpoint-20261008/xlang3.exe'
source = Path(r'C:\Python\Python314\Lib\site-packages\pyperformance\data-files\benchmarks\bm_sqlglot_v2\run_benchmark.py')
assert sha(source) == 'd97aaba24f8c49f2afe5c5c740c568be6e89dde3364d5a964a4b72e5a4768a9b'
hook = root / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
assert sha(hook) == '2ffc217fe20497156bfcb1cebc7fb87203f33c6d4d3dbb6ecbcbcf91ea4cb317'
files = [exe, exe.with_name('xlang3_runtime.dll'), root / 'src/runtime/value_hash.cpp', source, hook]
before = {str(p): sha(p) for p in files}
rows = json.loads(subprocess.check_output(['powershell', '-NoProfile', '-Command',
    'Get-CimInstance Win32_Process | Select-Object ProcessId,Name | ConvertTo-Json -Compress']).decode('utf-8-sig'))
assert not [r for r in rows if r['ProcessId'] != os.getpid() and
    (r['Name'].lower() in {'cl.exe','link.exe','ninja.exe','msbuild.exe','cmake.exe','ctest.exe'} or
     r['Name'].lower().startswith(('python','xlang3')))]
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
env['PYTHONPATH'] = str(hook.parent)
env['PYTHONIOENCODING'] = 'utf-8'
env.pop('PYTHONPYCACHEPREFIX', None)
command = [sys.executable, 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py',
    '--runtime', str(exe), '--benchmarks', 'sqlglot_v2_parse', '--mode', 'fast',
    '--case-timeout', '300', '--dependency-site',
    str(root / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'),
    '--output', str(output)]
record = dict(status='running', command=command, hashes_before=before)
def save(): receipt.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))
save()
try:
    with log.open('xb') as stream:
        result = subprocess.run(command, cwd=root, env=env, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=360)
    record['exit_code'] = result.returncode
    record['status'] = 'terminal' if result.returncode == 0 else 'failed'
except Exception as exc:
    record['status'] = 'failed'
    record['error'] = repr(exc)
    raise
finally:
    record['hashes_unchanged'] = all(sha(Path(p)) == value for p,value in before.items())
    record['log_sha256'] = sha(log)
    record['output_sha256'] = sha(output) if output.exists() else None
    record['terminal'] = True
    save()
assert result.returncode == 0 and record['hashes_unchanged']
print('Official SQLGlot parse completed, unchanged hashes', flush=True)
