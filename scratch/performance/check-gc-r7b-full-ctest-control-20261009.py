"""Untimed complete registered CTest audit on the unchanged accepted R7b engine."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CONTROL = ROOT / 'build-repro/controls/gc-generic-cycles-r7b-accepted-20261009'
CP = Path('C:/Python/Python314/python.exe')
CTEST = Path('C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe')
PREFIX = 'gc-r7b-full-ctest-control-20261009'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CP.resolve()
    manifest_path = CONTROL / 'provenance.json'
    assert sha(manifest_path) == 'aa2d9f715ad11e78ffa58024c10631c37e7d83d816c4a8cb0523f6469287547f'
    manifest = json.loads(manifest_path.read_bytes())
    ledger_path = DATA / 'pyperformance-xlang3-gc-r7b-per-definition-ownership-supplement-20261009-ledger.json'
    assert sha(ledger_path) == '1b4722611ba85bcd570ac6223c5106c1551eda67accafb3cf0309211f1196128'
    assert json.loads(ledger_path.read_bytes())['terminal']
    inventory_path = DATA / (PREFIX + '-inventory.stdout.log')
    inventory = json.loads(inventory_path.read_bytes())
    names = {t['name'] for t in inventory['tests']}
    assert len(inventory['tests']) == len(names) == 55
    assert not any('python313' in str(p).lower() for t in inventory['tests'] for p in t.get('command', []))
    pins = {str(ROOT / p): h for p, h in manifest['source_sha256'].items()}
    for directory, values in ((RELEASE, manifest['release_sha256']),
                              (ROOT / 'build-repro/Release', manifest['fixed_baseline_sha256']),
                              (CONTROL / 'sources', manifest['source_sha256']),
                              (CONTROL / 'Release', manifest['release_sha256']),
                              (ROOT, manifest['unowned_tracked_dirty_sha256'])):
        pins.update({str(directory / p): h for p, h in values.items()})
    for path in (CP, CTEST, manifest_path, inventory_path, ledger_path, Path(__file__),
                 ROOT / 'build-repro/main-verify-20261006/CTestTestfile.cmake'):
        pins[str(path)] = sha(path)
    for test in inventory['tests']:
        for value in test.get('command', []):
            path = Path(value)
            if path.is_file():
                pins[str(path)] = sha(path)
    def stable():
        return all(Path(p).is_file() and sha(p) == h for p, h in pins.items())
    assert stable()
    env = os.environ.copy()
    for key in ('PYTHONPATH', 'PYTHONHOME', 'PYTHONOPTIMIZE', 'PYTHONIOENCODING', 'PYTHONPYCACHEPREFIX', 'XLANG3_VM_OPCODE_TIMING'):
        env.pop(key, None)
    env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
    env['PATH'] = str(CP.parent) + os.pathsep + env.get('PATH', '')
    command = [str(CTEST), '--test-dir', str(ROOT / 'build-repro/main-verify-20261006'),
               '-C', 'Release', '--output-on-failure']
    stdout, stderr = (DATA / (PREFIX + suffix) for suffix in ('.stdout.log', '.stderr.log'))
    receipt = DATA / (PREFIX + '-receipt.json')
    assert not any(p.exists() for p in (stdout, stderr, receipt))
    record = {'terminal': False, 'status': 'running', 'passed': False, 'command': command,
              'registered_names': sorted(names), 'registered_count': len(names), 'pins': pins,
              'controller_sha256': sha(__file__), 'scope': 'Untimed full55 CTest control audit; no performance claim.'}
    def save():
        receipt.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
    save()
    child = None
    try:
        with stdout.open('xb') as out, stderr.open('xb') as err:
            child = subprocess.Popen(command, cwd=ROOT, env=env, stdout=out, stderr=err,
                                     stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
            record['pid'] = child.pid; save()
            print('Unchanged control full55 CTest started:', child.pid, flush=True)
            record['exit_code'] = child.wait(timeout=1800)
        output = stdout.read_text(encoding='utf-8', errors='replace')
        passed_names = re.findall(r'Test\s+#\d+:\s+(\S+)\s+.*?Passed', output)
        record['passed_names'] = passed_names
        record['passed'] = (record['exit_code'] == 0 and len(passed_names) == 55
                            and set(passed_names) == names and '100% tests passed, 0 tests failed out of 55' in output)
        record['status'] = 'full55_control_passed' if record['passed'] else 'full55_control_failed'
    except BaseException as error:
        record.update(status='control_audit_failed_or_invalid', error=repr(error))
    finally:
        try:
            if child is not None and child.poll() is None:
                subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=10, check=False)
                if child.poll() is None: child.kill()
                child.wait(timeout=10)
            record['cleanup_completed'] = child is not None and child.poll() is not None
        except BaseException as error:
            record['cleanup_error'] = repr(error)
        record['hashes_unchanged'] = stable()
        if not record.get('cleanup_completed') or not record['hashes_unchanged']:
            record.update(status='control_audit_failed_or_invalid', passed=False)
        record.update(terminal=True, stdout=stdout.name, stderr=stderr.name,
                      stdout_sha256=sha(stdout) if stdout.is_file() else None,
                      stderr_sha256=sha(stderr) if stderr.is_file() else None)
        save()
    print(json.dumps({'status': record['status'], 'receipt_sha256': sha(receipt)}, indent=2), flush=True)
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
