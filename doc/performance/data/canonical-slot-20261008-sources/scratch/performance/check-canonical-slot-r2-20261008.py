"""Reuse verified unchanged Python checks and repaired terminal C++ result."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
source = data / 'canonical-slot-early-20261008.json'
original = json.loads(source.read_text(encoding='utf-8'))
assert original['status'] == 'failed_cpp'
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
assert original['candidate_binary_sha256'] == {'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))}
for name, sha in original['source_sha256'].items():
    if name != 'tests/cpp/canonical_slot_read_cases.h':
        assert digest(root / name) == sha, name
record = {'status': 'running', 'candidate_binary_sha256': original['candidate_binary_sha256'],
          'source_sha256': {name: digest(root / name) for name in original['source_sha256']},
          'phases': [], 'reused_python_evidence': {'output': source.name, 'sha256': digest(source)}}
for phase in original['phases'][:3]:
    assert phase['exit_code'] == 0 and digest(data / phase['log']) == phase['sha256']
    record['phases'].append(phase)
cpp_log = data / 'canonical-slot-native-hooks-repaired-20261008.log'
assert '100% tests passed, 0 tests failed out of 1' in cpp_log.read_text(encoding='utf-8')
record['phases'].append({'name': 'cpp', 'exit_code': 0, 'log': cpp_log.name, 'sha256': digest(cpp_log),
                         'source': 'Authoritative ctest terminal exit 0 captured before this controller; no rerun',
                         'test_exe_sha256': digest(candidate.with_name('xlang3_interpreter_tests.exe'))})
output = data / 'canonical-slot-early-r2-20261008.json'
assert not output.exists()
def save():
    output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
save()
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
    env.pop(name, None)
completed = subprocess.run([sys.executable, 'scratch/performance/compare-canonical-slot-20261008.py'],
                           cwd=root, env=env, capture_output=True, timeout=300)
log = data / 'canonical-slot-early-r2-20261008-paired.log'
assert not log.exists()
log.write_bytes(completed.stdout + completed.stderr)
record['phases'].append({'name': 'paired', 'exit_code': completed.returncode, 'log': log.name, 'sha256': digest(log)})
record['status'] = 'terminal' if completed.returncode == 0 else 'failed_paired'
save()
assert completed.returncode == 0, completed.stdout + completed.stderr
assert record['candidate_binary_sha256'] == {'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))}
assert record['source_sha256'] == {name: digest(root / name) for name in record['source_sha256']}
print(completed.stdout.decode('utf-8'), flush=True)
