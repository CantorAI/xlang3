"""Compare instrumented phases and exact output fingerprints serially."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
body = json.loads((data / 'bpe-official-body-dict-index-observations-20261007.json').read_text(encoding='utf-8'))
assert body['status'] == 'terminal'
probe = root / 'scratch/performance/bpe-phase-diagnostic-20261007.py'
out = data / 'bpe-dict-index-phase-observations-20261007.json'
assert not out.exists()
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
env['PYTHONPATH'] = os.pathsep.join((str(root / 'benchmarks/diagnostics/pyperf_compat'),
                                  str(root / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages')))
env['PYTHONIOENCODING'] = 'utf-8'
env.pop('PYTHONPYCACHEPREFIX', None)
record = {'status': 'running', 'probe_sha256': hashlib.sha256(probe.read_bytes()).hexdigest(),
          'scope': 'instrumented full algorithm, phase diagnosis only, no benchmark score', 'observations': []}
for label, runtime in (('cpython3147', sys.executable), ('candidate', str(root / 'build-repro/main-verify-20261006/Release/xlang3.exe'))):
    log = data / ('bpe-dict-index-phase-' + label + '-20261007.log')
    assert not log.exists()
    print('Starting full BPE phase diagnostic:', label, flush=True)
    with log.open('w', encoding='utf-8') as stream:
        result = subprocess.run([runtime, str(probe)], env=env, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=120)
    row = {'runtime': label, 'exit_code': result.returncode, 'log': log.name,
           'log_sha256': hashlib.sha256(log.read_bytes()).hexdigest()}
    if result.returncode == 0:
        row['result'] = json.loads(log.read_text(encoding='utf-8'))
        print(row['result'], flush=True)
    else:
        print(log.read_text(encoding='utf-8')[-1500:], flush=True)
    record['observations'].append(row)
    out.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    assert result.returncode == 0
left, right = [row['result'] for row in record['observations']]
for field in ('vocab_size', 'ranks_sha256', 'encoded_token_count', 'encoded_tokens_sha256'):
    assert left[field] == right[field], field
record['status'] = 'terminal_matching_complete_outputs'
out.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print('All phases complete; ranks and encoded tokens match CPython', flush=True)
