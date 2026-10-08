"""Require focused correctness before collecting paired slot/SQLGlot timings."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
prefix = 'canonical-slot-early-20261008'
output = data / (prefix + '.json')
assert not output.exists()
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
control = root / 'build-repro/controls/vm-captured-lookup-checkpoint-20261007/xlang3.exe'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
sources = ('src/executor/xlang_vm/xlang_vm_attr.cpp', 'src/executor/xlang_vm/ops/xlang_vm_ops_attr.h',
           'tests/cpp/canonical_slot_read_cases.h', 'tests/cpp/interpreter_tests.cpp',
           'tests/run_fixtures.py', 'tests/fixtures/core/canonical_slot_reads.py',
           'tests/fixtures/expected/canonical_slot_reads.out')
record = {'status': 'running', 'candidate_binary_sha256': {'exe': digest(candidate),
          'dll': digest(candidate.with_name('xlang3_runtime.dll'))},
          'source_sha256': {name: digest(root / name) for name in sources}, 'phases': []}
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
    env.pop(name, None)
def save():
    output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
def phase(name, command, timeout):
    completed = subprocess.run(command, cwd=root, env=env, capture_output=True, timeout=timeout)
    log = data / (prefix + '-' + name + '.log')
    assert not log.exists()
    log.write_bytes(completed.stdout + completed.stderr)
    record['phases'].append({'name': name, 'command': command, 'exit_code': completed.returncode,
                             'log': log.name, 'sha256': digest(log)})
    record['status'] = 'running' if completed.returncode == 0 else 'failed_' + name
    save()
    assert completed.returncode == 0, completed.stdout + completed.stderr
    print('Passed', name, flush=True)
    return completed.stdout
save()
gold = (root / sources[-1]).read_bytes().decode('utf-8').replace('\r\n', '\n').strip()
for label, executable in (('cpython3147', Path(sys.executable)), ('control', control), ('candidate', candidate)):
    stdout = phase(label, [str(executable), 'tests/fixtures/core/canonical_slot_reads.py'], 90)
    assert stdout.decode('utf-8').replace('\r\n', '\n').strip() == gold, label
ctest = r'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
phase('cpp', [ctest, '--test-dir', 'build-repro/main-verify-20261006', '-C', 'Release',
              '-R', 'xlang3_interpreter_tests', '--output-on-failure'], 120)
phase('paired', [sys.executable, 'scratch/performance/compare-canonical-slot-20261008.py'], 300)
assert record['candidate_binary_sha256'] == {'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))}
assert record['source_sha256'] == {name: digest(root / name) for name in sources}
record['status'] = 'terminal'
save()
print('Early check terminal', flush=True)
