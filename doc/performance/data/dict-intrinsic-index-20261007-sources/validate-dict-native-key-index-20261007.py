"""Sequential correctness, fixed gate and official affected-case validation."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
baseline = root / 'build-repro/Release/xlang3.exe'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert digest(baseline) == 'a5f5028c15e145edce645a5afc25c11fbce77f51e882312b1fbe06e63c72a4af'
assert digest(baseline.with_name('xlang3_runtime.dll')) == 'bc1b9c0a8086f7e6fb0c037516dc9c1eea20427fa887e3aa623714bc5ef5da8d'
prefix = 'dict-native-key-index-validation-r2-20261007'
output = data / (prefix + '.json')
assert not output.exists()
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
env.pop('PYTHONIOENCODING', None)
env.pop('PYTHONPYCACHEPREFIX', None)
env.pop('PYTHONPATH', None)
binary = {'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))}
record = {'status': 'running', 'candidate_binary_sha256': binary, 'phases': []}


def phase(name, command, timeout):
    log = data / (prefix + '-' + name + '.log')
    assert not log.exists()
    print('Starting', name, flush=True)
    with log.open('w', encoding='utf-8') as stream:
        result = subprocess.run(command, cwd=root, env=env, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=timeout)
    record['phases'].append({'name': name, 'command': command, 'exit_code': result.returncode,
                             'log': log.name, 'sha256': digest(log)})
    record['status'] = 'running' if result.returncode == 0 else 'failed_' + name
    output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print('Finished', name, 'exit', result.returncode, flush=True)
    assert result.returncode == 0, name


spec = importlib.util.spec_from_file_location('fixtures', root / 'tests/run_fixtures.py')
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)
record['fixture_counts'] = {'core': len(fixtures.CORE_CASES), 'compatibility_sections': len(fixtures.SECTION_CASES), 'expected_failures': 3}
phase('fixtures', [sys.executable, 'tests/run_fixtures.py', str(candidate)], 240)
ctest = r'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
phase('cpp', [ctest, '--test-dir', 'build-repro/main-verify-20261006', '-C', 'Release', '-R',
              'xlang3_(runtime_value_tests|interpreter_tests|sdk_stream_call_tests|graph_)', '--output-on-failure'], 240)
probe = root / 'scratch/performance/dict-composite-scaling-probe-20261007.py'
phase('scaling', [str(candidate), str(probe)], 120)
scaling = json.loads((data / (prefix + '-scaling.log')).read_text(encoding='utf-8'))
record['scaling_probe_sha256'] = digest(probe)
record['scaling_rows'] = scaling['rows']
for row in scaling['rows']:
    print('Scaling', row['shape'], 'counter', row['counter'], 'fresh', row['fresh_pair'],
          'keys', row['key_count'], 'median_seconds', statistics.median(row['samples_seconds']), flush=True)
gate = data / 'release-dict-native-key-index-fixed-gate-20261007.json'
assert not gate.exists()
phase('gate', [sys.executable, 'benchmarks/check_regression.py', '--baseline', str(baseline),
               '--candidate', str(candidate), '--output', str(gate)], 600)
gate_record = json.loads(gate.read_text(encoding='utf-8'))
assert gate_record['status'] == 'pass' and len(gate_record['cases']) == 11
assert (gate_record['repeats'], gate_record['warmup'], gate_record['threshold']) == (21, 5, .1)
record['fixed_gate'] = {'output': gate.name, 'sha256': digest(gate), 'exit_code': 0}
official = data / 'pyperformance-dict-native-key-index-bpe-fast-20261007.json'
assert not official.exists()
phase('official-bpe', [sys.executable, 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py',
                       '--runtime', str(candidate), '--benchmarks', 'bpe_tokeniser', '--mode', 'fast',
                       '--case-timeout', '300', '--dependency-site',
                       str(root / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'),
                       '--output', str(official)], 360)
assert {'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))} == binary
record['status'] = 'correctness_fixed_gate_and_official_bpe_passed'
record['official_bpe'] = {'output': official.name, 'sha256': digest(official)}
output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print('Validation terminal: correctness, fixed gate and official BPE passed', flush=True)
