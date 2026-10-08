"""Observe prepared diagnostics serially after the official run is terminal."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
validation = json.loads((data / 'dict-native-index-final-validation-20261007.json').read_text(encoding='utf-8'))
assert validation['status'] != 'running' and len(validation['phases']) == 5
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert digest(candidate.with_name('xlang3_runtime.dll')) == validation['candidate_binary_sha256']['dll']
prefix = 'dict-native-dispatch-followup-20261007'
out = data / (prefix + '.json')
assert not out.exists()
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
    env.pop(name, None)
record = {'status': 'running', 'candidate_binary_sha256': validation['candidate_binary_sha256'],
          'scope': 'diagnostic timings and semantic observations; never an official score', 'observations': []}
for name, filename in (('inherited_subscripts', 'inherited-subscript-dispatch-probe-20261007.py'),
                       ('callback_boundary', 'dict-callback-boundary-probe-20261007.py'),
                       ('minmax_order', 'minmax-streaming-order-probe-20261007.py')):
    probe = root / 'scratch/performance' / filename
    for label, runtime in (('cpython3147', Path(sys.executable)), ('candidate', candidate)):
        log = data / (prefix + '-' + name + '-' + label + '.log')
        assert not log.exists()
        print('Starting', name, label, flush=True)
        with log.open('w', encoding='utf-8') as stream:
            result = subprocess.run([str(runtime), str(probe)], env=env, stdout=stream,
                                    stderr=subprocess.STDOUT, timeout=60)
        row = {'probe': name, 'runtime': label, 'exit_code': result.returncode,
               'probe_sha256': digest(probe), 'log': log.name, 'log_sha256': digest(log)}
        if result.returncode == 0:
            row['result'] = json.loads(log.read_text(encoding='utf-8'))
        else:
            row['error_output'] = log.read_text(encoding='utf-8')[-2000:]
        record['observations'].append(row)
        out.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        print('Finished', name, label, 'exit', result.returncode, flush=True)
assert digest(candidate.with_name('xlang3_runtime.dll')) == validation['candidate_binary_sha256']['dll']
record['status'] = 'terminal'
out.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print('All six observations terminal', flush=True)
