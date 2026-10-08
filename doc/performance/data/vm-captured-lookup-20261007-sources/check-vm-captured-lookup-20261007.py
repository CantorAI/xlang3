"""Fast semantic check then paired probe; stop before dependent work on failure."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
prefix = 'vm-captured-lookup-early-20261007'
output = data / (prefix + '.json')
assert not output.exists()
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
sources = ('src/executor/xlang_vm/ops/xlang_vm_ops_call.h', 'tests/run_fixtures.py',
           'tests/fixtures/core/vm_captured_lookup.py', 'tests/fixtures/expected/vm_captured_lookup.out')
record = {'status': 'running', 'candidate_binary_sha256': {'exe': digest(candidate),
          'dll': digest(candidate.with_name('xlang3_runtime.dll'))},
          'source_sha256': {name: digest(root / name) for name in sources}, 'phases': []}
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
    env.pop(name, None)
def save():
    output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
save()
for label, executable in (('cpython3147', Path(sys.executable)), ('xlang3', candidate)):
    command = [str(executable), 'tests/fixtures/core/vm_captured_lookup.py']
    completed = subprocess.run(command, cwd=root, env=env, capture_output=True, timeout=90)
    log = data / (prefix + '-' + label + '.log')
    assert not log.exists()
    log.write_bytes(completed.stdout + completed.stderr)
    record['phases'].append({'name': label, 'command': command, 'exit_code': completed.returncode,
                             'log': log.name, 'sha256': digest(log)})
    record['status'] = 'semantic_running' if completed.returncode == 0 else 'failed_' + label
    save()
    assert completed.returncode == 0, completed.stdout + completed.stderr
    normalize = lambda raw: raw.decode('utf-8').replace('\r\n', '\n').strip()
    assert normalize(completed.stdout) == normalize((root / sources[-1]).read_bytes())
    print(label, 'fixture passed', flush=True)
paired = subprocess.run([sys.executable, 'scratch/performance/compare-vm-captured-lookup-20261007.py'],
                        cwd=root, env=env, capture_output=True, timeout=300)
log = data / (prefix + '-paired.log')
assert not log.exists()
log.write_bytes(paired.stdout + paired.stderr)
record['phases'].append({'name': 'paired', 'exit_code': paired.returncode, 'log': log.name, 'sha256': digest(log)})
record['status'] = 'terminal' if paired.returncode == 0 else 'failed_paired'
save()
assert paired.returncode == 0, paired.stdout + paired.stderr
assert record['candidate_binary_sha256'] == {'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))}
assert record['source_sha256'] == {name: digest(root / name) for name in sources}
print(paired.stdout.decode('utf-8'), flush=True)
