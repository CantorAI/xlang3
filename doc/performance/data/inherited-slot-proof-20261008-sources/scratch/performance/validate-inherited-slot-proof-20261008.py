"""Serial full correctness, unchanged fixed gate, and official SQLGlot validation."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
assert Path(sys.executable).resolve() == Path(r'C:\Python\Python314\python.exe').resolve()
root = Path.cwd()
data = root / 'doc/performance/data'
prefix = 'inherited-slot-proof-validation-20261008'
output = data / (prefix + '.json')
assert not output.exists()
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
baseline = root / 'build-repro/Release/xlang3.exe'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert digest(baseline) == 'a5f5028c15e145edce645a5afc25c11fbce77f51e882312b1fbe06e63c72a4af'
assert digest(baseline.with_name('xlang3_runtime.dll')) == 'bc1b9c0a8086f7e6fb0c037516dc9c1eea20427fa887e3aa623714bc5ef5da8d'
early_path = data / 'inherited-slot-proof-early-20261008.json'
record = json.loads(early_path.read_text(encoding='utf-8'))
assert record['status'] == 'terminal'
assert record['hashes_unchanged']
record['candidate_binary_sha256'] = record['hashes_before']['binaries_sha256']['candidate']
record['source_sha256'] = record['hashes_before']['source_sha256']
assert record['candidate_binary_sha256'] == {'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))}
assert all(digest(root / name) == sha for name, sha in record['source_sha256'].items())
paired_path = data / 'inherited-slot-proof-paired-20261008.json'
paired = json.loads(paired_path.read_text(encoding='utf-8'))
assert paired['status'] == 'terminal'
# These unchanged negative controls rejected R3 before expensive validation.
for probe in paired['probes']:
    for row in probe['summary']:
        if row['identity'].get('path') in ('inherited_slot', 'ordinary_attr'):
            assert row['median_speedup'] >= .9, row
record['early_evidence'] = {'output': early_path.name, 'sha256': digest(early_path)}
record['paired_evidence'] = {'output': paired_path.name, 'sha256': digest(paired_path)}
record['phases'] = []
record['status'] = 'running'
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
    env.pop(name, None)

def save():
    output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))

def idle():
    script = "Get-CimInstance Win32_Process | Where-Object { $_.Name -match '^(xlang3|cl|ninja|MSBuild|cmake|python)(\\.exe)?$' } | Select-Object Name,ProcessId,CommandLine | ConvertTo-Json -Compress"
    result = subprocess.run(['powershell', '-NoProfile', '-Command', script], capture_output=True, check=True)
    rows = json.loads(result.stdout.decode('utf-8-sig') or '[]')
    if isinstance(rows, dict):
        rows = [rows]
    conflicts = [row for row in rows if row['ProcessId'] != os.getpid()]
    assert not conflicts, conflicts

def phase(name, command, timeout, required=True):
    idle()
    log = data / (prefix + '-' + name + '.log')
    assert not log.exists()
    print('Starting', name, flush=True)
    timed_out = False
    with log.open('wb') as stream:
        try:
            completed = subprocess.run(command, cwd=root, env=env, stdout=stream,
                                       stderr=subprocess.STDOUT, timeout=timeout)
            code = completed.returncode
        except subprocess.TimeoutExpired:
            code, timed_out = 124, True
    record['phases'].append({'name': name, 'command': command, 'exit_code': code,
                             'timeout': timed_out, 'log': log.name, 'sha256': digest(log)})
    record['status'] = 'running' if code == 0 else 'failed_' + name
    save()
    print('Finished', name, 'exit', code, flush=True)
    if required:
        assert code == 0, name
    return code

idle()
save()
spec = importlib.util.spec_from_file_location('fixture_counts', root / 'tests/run_fixtures.py')
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)
record['fixture_counts'] = {'core': len(fixtures.CORE_CASES),
                           'compatibility_sections': len(fixtures.SECTION_CASES), 'expected_failures': 3}
phase('fixtures', [sys.executable, 'tests/run_fixtures.py', str(candidate)], 240)
ctest = r'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
phase('cpp', [ctest, '--test-dir', 'build-repro/main-verify-20261006', '-C', 'Release',
              '-R', 'xlang3_(runtime_value_tests|interpreter_tests|sdk_stream_call_tests|graph_)',
              '--output-on-failure'], 240)
gate_path = data / 'release-inherited-slot-proof-fixed-gate-20261008.json'
assert not gate_path.exists()
phase('gate', [sys.executable, 'benchmarks/check_regression.py', '--baseline', str(baseline),
               '--candidate', str(candidate), '--output', str(gate_path)], 600)
gate = json.loads(gate_path.read_text(encoding='utf-8'))
assert gate['status'] == 'pass' and len(gate['cases']) == 11
assert (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .1)
record['fixed_gate'] = {'output': gate_path.name, 'sha256': digest(gate_path), 'exit_code': 0}
hook = root / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
assert digest(hook) == '2ffc217fe20497156bfcb1cebc7fb87203f33c6d4d3dbb6ecbcbcf91ea4cb317'
record['compatibility_hook_sha256'] = digest(hook)
env['PYTHONPATH'] = str(hook.parent)
env['PYTHONIOENCODING'] = 'utf-8'
save()
official = data / 'pyperformance-inherited-slot-proof-sqlglot-parse-fast-20261008.json'
assert not official.exists()
code = phase('official-sqlglot-parse', [sys.executable, 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py',
              '--runtime', str(candidate), '--benchmarks', 'sqlglot_v2_parse', '--mode', 'fast',
              '--case-timeout', '300', '--dependency-site',
              str(root / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'),
              '--output', str(official)], 360, required=False)
assert record['candidate_binary_sha256'] == {'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))}
assert record['source_sha256'] == {name: digest(root / name) for name in record['source_sha256']}
record['official_sqlglot_parse'] = {'output': official.name,
                                 'sha256': digest(official) if official.exists() else None, 'exit_code': code}
record['status'] = 'validated' if code == 0 else 'correctness_and_gate_passed_official_failed'
save()
print('Validation terminal:', record['status'], flush=True)
