"""Run the prepared unscored source once and retain the emitted IR provenance."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

root = Path.cwd()
scratch = root / 'scratch/performance'
metadata = scratch / 'bpe-callback-ir-source-20261007.json'
record = json.loads(metadata.read_text(encoding='utf-8'))
source = scratch / 'bpe-callback-ir-source-20261007.py'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert digest(source) == record['derived_sha256']
runtime = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
debug = scratch / 'bpe-callback-ir-20261007'
log = scratch / 'bpe-callback-ir-20261007.log'
assert not debug.exists() and not log.exists()
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
env['PYTHONPATH'] = os.pathsep.join((str(root / 'benchmarks/diagnostics/pyperf_compat'),
                                  str(root / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages')))
for name in ('PYTHONIOENCODING', 'PYTHONPYCACHEPREFIX'):
    env.pop(name, None)
command = [str(runtime), '--dump-ir', '--debug-dir', str(debug), str(source)]
with log.open('w', encoding='utf-8') as stream:
    result = subprocess.run(command, cwd=root, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=60)
record['runtime_execution'] = 'terminal'
record['exit_code'] = result.returncode
record['command'] = command
record['binary_sha256'] = {'exe': digest(runtime), 'dll': digest(runtime.with_name('xlang3_runtime.dll'))}
record['log_sha256'] = digest(log)
record['ir_files_sha256'] = {str(path.relative_to(root)): digest(path) for path in debug.rglob('*.ir.txt')}
metadata.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print('IR diagnostic exit', result.returncode, 'files', len(record['ir_files_sha256']))
assert result.returncode == 0
