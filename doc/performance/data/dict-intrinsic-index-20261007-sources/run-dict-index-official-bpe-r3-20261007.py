"""Retry only official BPE with the unchanged full-suite compatibility hook."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
validation_path = data / 'dict-native-key-index-validation-r2-20261007.json'
validation = json.loads(validation_path.read_text(encoding='utf-8'))
assert [phase['exit_code'] for phase in validation['phases'][:4]] == [0, 0, 0, 0]
binary = {'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))}
assert binary == validation['candidate_binary_sha256']
hook = root / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
full = json.loads((data / 'pyperformance-xlang3-live-eval-full-fast-20261007-provenance.json').read_text(encoding='utf-8'))
assert digest(hook) == full['compatibility_hook_sha256']
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
env['PYTHONPATH'] = str(hook.parent)
env['PYTHONIOENCODING'] = 'utf-8'
env.pop('PYTHONPYCACHEPREFIX', None)
prefix = 'pyperformance-dict-native-key-index-bpe-fast-r3-20261007'
output = data / (prefix + '.json')
log = data / (prefix + '.log')
proof = data / (prefix + '-provenance.json')
assert not any(path.exists() for path in (output, log, proof))
command = [sys.executable, 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py',
           '--runtime', str(candidate), '--benchmarks', 'bpe_tokeniser', '--mode', 'fast',
           '--case-timeout', '300', '--dependency-site',
           str(root / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'),
           '--output', str(output)]
record = {'status': 'running', 'command': command, 'candidate_binary_sha256': binary,
          'compatibility_hook_sha256': digest(hook), 'stdlib': r'C:\Python\Python314\Lib',
          'reused_validation': validation_path.name, 'reused_validation_sha256': digest(validation_path),
          'scope': 'Retry only the affected official case; preserve prior harness failure; no repeated build/gate'}
proof.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print('Official BPE started with verified full-suite shim', flush=True)
with log.open('w', encoding='utf-8') as stream:
    result = subprocess.run(command, cwd=root, env=env, stdout=stream,
                            stderr=subprocess.STDOUT, timeout=360)
assert binary == {'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))}
record.update(status='finished' if result.returncode == 0 else 'finished_with_benchmark_failure',
              exit_code=result.returncode, log_sha256=digest(log),
              output_sha256=digest(output) if output.exists() else None)
proof.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print('Official BPE terminal exit', result.returncode, flush=True)
print(log.read_text(encoding='utf-8')[-3000:], flush=True)
