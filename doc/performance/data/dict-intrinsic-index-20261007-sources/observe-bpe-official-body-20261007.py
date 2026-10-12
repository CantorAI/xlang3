"""Serial fresh-process full-body observations after terminal official run."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
trial = json.loads((data / 'dict-native-read-index-validation-20261007.json').read_text(encoding='utf-8'))
assert trial['status'] != 'running'
assert len(trial['phases']) == 5
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
control = root / 'build-repro/controls/live-eval-checkpoint-20261007/xlang3.exe'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert digest(candidate.with_name('xlang3_runtime.dll')) == trial['candidate_binary_sha256']['dll']
prefix = 'bpe-official-body-dict-index-observations-20261007'
output = data / (prefix + '.json')
assert not output.exists()
probe = root / 'scratch/performance/bpe-official-body-once-20261007.py'
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
env['PYTHONPATH'] = os.pathsep.join((str(root / 'benchmarks/diagnostics/pyperf_compat'),
                                  str(root / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages')))
env['PYTHONIOENCODING'] = 'utf-8'
env.pop('PYTHONPYCACHEPREFIX', None)
record = {'status': 'running', 'scope': 'full unchanged official body, diagnostic only; no calibration or suite score',
          'probe_sha256': digest(probe), 'observations': []}


def save():
    output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')


save()
for name, runtime in (('cpython3147', Path(sys.executable)), ('candidate', candidate), ('accepted_control', control)):
    log = data / (prefix + '-' + name + '.log')
    assert not log.exists()
    observation = {'runtime': name, 'executable': str(runtime), 'exe_sha256': digest(runtime)}
    if name != 'cpython3147':
        observation['dll_sha256'] = digest(runtime.with_name('xlang3_runtime.dll'))
    print('Starting full official body:', name, flush=True)
    with log.open('w', encoding='utf-8') as stream:
        try:
            result = subprocess.run([str(runtime), str(probe)], cwd=root, env=env,
                                    stdout=stream, stderr=subprocess.STDOUT, timeout=180)
            observation['exit_code'] = result.returncode
        except subprocess.TimeoutExpired:
            observation['exit_code'] = None
            observation['timeout_seconds'] = 180
    observation.update(log=log.name, log_sha256=digest(log))
    if observation['exit_code'] == 0:
        observation['result'] = json.loads(log.read_text(encoding='utf-8'))
        print('Body seconds:', observation['result']['body_seconds'], flush=True)
    else:
        print('Body failed or timed out:', observation, flush=True)
    record['observations'].append(observation)
    save()
assert digest(candidate.with_name('xlang3_runtime.dll')) == trial['candidate_binary_sha256']['dll']
record['status'] = 'terminal'
save()
print('All three serial body observations terminal', flush=True)
