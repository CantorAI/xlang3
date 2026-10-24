"""Fresh CPython oracle and full correctness for generic cycle discovery."""
import hashlib
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
APP = DATA / 'gc-generic-cycles-applied-source-r6-20261009.json'
BUILD = DATA / 'gc-generic-cycles-build-r6-20261009.json'
PARENT = DATA / 'python-new-vm-continuation-r4-correctness-r2-20261009.json'
PREFIX = 'gc-generic-cycles-correctness-r6-20261009'

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def read(path):
    return json.loads(Path(path).read_text())

def main():
    assert sys.version_info[:3] == (3, 14, 7)
    assert sha(APP) == '22d1313f3f244ba0a4c8a488c5f8a01bcecbb2758609405bfda5d02baa44f71e'
    assert BUILD.is_file()
    application = read(APP); build = read(BUILD)
    assert build['passed'] and build['sources_unchanged']
    assert all(sha(ROOT / p) == h for p, h in application['source_sha256'].items())
    binaries = build['release_sha256']
    assert all(sha(RELEASE / p) == h for p, h in binaries.items())
    fixture_module = runpy.run_path(str(ROOT / 'tests/run_fixtures.py'))
    counts = {'core': len(fixture_module['CORE_CASES']), 'compat_sections': len(fixture_module['SECTION_CASES']),
              'expected_failures': 3}
    phases = [{'name': 'gc-generic-cycles-reference-transcript', 'command': [str(RELEASE / 'xlang3.exe'),
               str(ROOT / 'tests/fixtures/core/gc_generic_cycles.py')]}]
    phases.insert(0, {'name': 'cpython3147-reference-transcript', 'command': ['C:/Python/Python314/python.exe', '-I', str(ROOT / 'tests/fixtures/core/gc_generic_cycles.py')]})
    phases += [{'name': p['name'], 'command': p['command']} for p in read(PARENT)['phases']]
    env = os.environ.copy()
    for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE', 'PYTHONHOME', 'XLANG3_VM_OPCODE_TIMING'):
        env.pop(name, None)
    env['XLANG3_PYTHON_LIB'] = 'C:/Python/Python314/Lib'
    receipt = DATA / (PREFIX + '.json')
    assert not receipt.exists()
    record = {'status': 'correctness_running', 'terminal': False, 'phases': [],
        'fixture_counts': counts, 'source_count': application['source_count'],
        'source_sha256': application['source_sha256'], 'release_sha256': binaries,
        'application_sha256': sha(APP), 'build_sha256': sha(BUILD), 'controller_sha256': sha(__file__),
        'scope': 'Fresh correctness; no timing claim or gate reuse.'}
    def save():
        receipt.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
    save()
    all_passed = True
    for phase in phases:
        name = phase['name']
        stdout = DATA / (PREFIX + '-' + name + '.stdout.log')
        stderr = DATA / (PREFIX + '-' + name + '.stderr.log')
        assert not stdout.exists() and not stderr.exists()
        with stdout.open('xb') as out, stderr.open('xb') as err:
            result = subprocess.run(phase['command'], cwd=ROOT, env=env, stdout=out, stderr=err,
                                    stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW, timeout=600)
        passed = result.returncode == 0
        text = stdout.read_text(encoding='utf-8', errors='replace')
        if name.endswith('reference-transcript'):
            expected = (ROOT / 'tests/fixtures/expected/gc_generic_cycles.out').read_text(encoding='utf-8')
            passed &= text.replace('\r', '').strip() == expected.replace('\r', '').strip()
        if name == 'ctest-inventory':
            inventory = json.loads(text)
            record['ctest_count'] = len(inventory['tests'])
            passed &= record['ctest_count'] == 9
        row = {**phase, 'exit_code': result.returncode, 'passed': passed,
               'stdout': stdout.name, 'stdout_sha256': sha(stdout), 'stderr': stderr.name, 'stderr_sha256': sha(stderr)}
        record['phases'].append(row)
        save()
        print(name + ': ' + ('PASS' if passed else 'FAIL'), flush=True)
        all_passed &= passed
        if not passed:
            break
    source_unchanged = all(sha(ROOT / p) == h for p, h in application['source_sha256'].items())
    release_unchanged = all(sha(RELEASE / p) == h for p, h in binaries.items())
    baseline_unchanged = all(sha(ROOT / 'build-repro/Release' / p) == h
                             for p, h in application['fixed_baseline_sha256'].items())
    record.update(status='correctness_passed_performance_pending' if all_passed and source_unchanged and release_unchanged and baseline_unchanged else 'correctness_failed',
        terminal=True, correctness_passed=all_passed, sources_unchanged=source_unchanged,
        release_unchanged=release_unchanged, fixed_baseline_unchanged=baseline_unchanged,
        performance_gate_passed=False, engine_commit_permitted=False)
    save()
    print(json.dumps({'status': record['status'], 'fixture_counts': counts, 'receipt_sha256': sha(receipt)}, indent=2), flush=True)
    return 0 if record['status'] == 'correctness_passed_performance_pending' else 1

if __name__ == '__main__':
    raise SystemExit(main())
