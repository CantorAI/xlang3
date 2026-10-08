"""Validate generic native trivial-function entry once, serially, using the fixed run path."""
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
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
assert digest(baseline) == 'a5f5028c15e145edce645a5afc25c11fbce77f51e882312b1fbe06e63c72a4af'
assert digest(baseline.with_name('xlang3_runtime.dll')) == 'bc1b9c0a8086f7e6fb0c037516dc9c1eea20427fa887e3aa623714bc5ef5da8d'
prefix = 'captured-lookup-validation-r2-20261007'
output = data / (prefix + '.json')
assert not output.exists()
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONIOENCODING', 'PYTHONPYCACHEPREFIX', 'PYTHONPATH'):
    env.pop(name, None)
binary = {'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))}
sources = ('src/internal/xlang3/builtin_methods.h', 'src/internal/xlang3/mapping.h',
           'src/runtime/methods/dict_methods.cpp', 'src/runtime/mapping.cpp',
           'src/executor/xlang_vm/xlang_vm_inline_call.h', 'src/runtime/functional_iterators.cpp',
           'tests/cpp/mapping_iterator_ownership_cases.h', 'tests/run_fixtures.py',
           'tests/fixtures/core/native_captured_lookup.py',
           'tests/fixtures/expected/native_captured_lookup.out')
record = {'status': 'running', 'candidate_binary_sha256': binary,
          'source_sha256': {p: digest(root / p) for p in sources}, 'phases': []}


def save():
    output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')


def phase(name, command, timeout, required=True):
    log = data / (prefix + '-' + name + '.log')
    assert not log.exists()
    print('Starting', name, flush=True)
    with log.open('w', encoding='utf-8') as stream:
        result = subprocess.run(command, cwd=root, env=env, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=timeout)
    record['phases'].append({'name': name, 'command': command,
                            'exit_code': result.returncode,
                            'log': log.name, 'sha256': digest(log)})
    record['status'] = 'running' if result.returncode == 0 else 'failed_' + name
    save()
    print('Finished', name, 'exit', result.returncode, flush=True)
    if required:
        assert result.returncode == 0, name
    return result.returncode


save()
spec = importlib.util.spec_from_file_location('fixtures', root / 'tests/run_fixtures.py')
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)
record['fixture_counts'] = {'core': len(fixtures.CORE_CASES),
                          'compatibility_sections': len(fixtures.SECTION_CASES),
                          'expected_failures': 3}
phase('fixtures', [sys.executable, 'tests/run_fixtures.py', str(candidate)], 240)
ctest = r'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
phase('cpp', [ctest, '--test-dir', 'build-repro/main-verify-20261006', '-C', 'Release',
              '-R', 'xlang3_(runtime_value_tests|interpreter_tests|sdk_stream_call_tests|graph_)',
              '--output-on-failure'], 240)
probe = root / 'scratch/performance/dict-composite-scaling-probe-20261007.py'
phase('scaling', [str(candidate), str(probe)], 120)
scaling = json.loads((data / (prefix + '-scaling.log')).read_text(encoding='utf-8'))
record['scaling_probe_sha256'] = digest(probe)
record['scaling_rows'] = scaling['rows']
for row in scaling['rows']:
    print('Scaling', row['shape'], 'counter', row['counter'], 'fresh', row['fresh_pair'],
          'keys', row['key_count'], 'median_seconds', statistics.median(row['samples_seconds']), flush=True)
phase('paired-callbacks', [sys.executable, 'scratch/performance/compare-captured-lookup-trial-20261007.py'], 300)
record['paired_callbacks'] = 'captured-lookup-paired-20261007.json'
gate = data / 'release-captured-lookup-r2-fixed-gate-20261007.json'
assert not gate.exists()
phase('gate', [sys.executable, 'benchmarks/check_regression.py', '--baseline', str(baseline),
               '--candidate', str(candidate), '--output', str(gate)], 600)
gate_record = json.loads(gate.read_text(encoding='utf-8'))
assert gate_record['status'] == 'pass' and len(gate_record['cases']) == 11
assert (gate_record['repeats'], gate_record['warmup'], gate_record['threshold']) == (21, 5, .1)
record['fixed_gate'] = {'output': gate.name, 'sha256': digest(gate), 'exit_code': 0}
hook = root / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
assert digest(hook) == '2ffc217fe20497156bfcb1cebc7fb87203f33c6d4d3dbb6ecbcbcf91ea4cb317'
env['PYTHONPATH'] = str(hook.parent)
env['PYTHONIOENCODING'] = 'utf-8'
record['compatibility_hook_sha256'] = digest(hook)
save()
official = data / 'pyperformance-captured-lookup-r2-bpe-fast-20261007.json'
assert not official.exists()
code = phase('official-bpe', [sys.executable, 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py',
                             '--runtime', str(candidate), '--benchmarks', 'bpe_tokeniser', '--mode', 'fast',
                             '--case-timeout', '1800', '--dependency-site',
                             str(root / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'),
                             '--output', str(official)], 1860, required=False)
assert binary == {'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))}
assert record['source_sha256'] == {p: digest(root / p) for p in sources}
record['status'] = 'validated' if code == 0 else 'correctness_and_gate_passed_official_failed'
record['official_bpe'] = {'output': official.name,
                          'sha256': digest(official) if official.exists() else None,
                          'exit_code': code}
save()
print('Validation terminal:', record['status'], flush=True)
