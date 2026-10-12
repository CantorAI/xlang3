"""Fourteen targeted correctness phases before any speed claim; no retries."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
INVENTORY = DATA / 'sorted-exact-int-s8-applied-source-20261008.json'
INVENTORY_SHA = '38abf457638ca411c6ed67b50f727ca1a1375ec08e4745b3cd5d2402dca863bc'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
PREFIX = 'sorted-exact-int-s8-focused-20261008'

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    assert sys.version_info[:3] == (3, 14, 7)
    assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
    assert sha(INVENTORY) == INVENTORY_SHA
    inventory = json.loads(INVENTORY.read_bytes())
    assert len(inventory['source_sha256']) == inventory['source_count'] and inventory['source_count'] >= 85
    assert not any(DATA.glob(PREFIX + '*')), 'Preserve prior evidence'
    sources = inventory['source_sha256']
    binaries = {p.relative_to(ROOT).as_posix(): sha(p) for p in RELEASE.rglob('*') if p.is_file()}
    tracked = {str(ROOT / p): h for p, h in {**sources, **binaries}.items()}
    tracked.update({str(INVENTORY): INVENTORY_SHA, str(Path(__file__)): sha(Path(__file__))})
    def verify():
        return all(Path(p).is_file() and sha(p) == h for p, h in tracked.items())
    assert verify()
    record = {'status': 'running_targeted_correctness', 'terminal': False,
              'full_validated': False, 'source_inventory_sha256': INVENTORY_SHA,
              'source_sha256': sources, 'binaries_sha256': binaries,
              'candidate_binary_sha256': {'exe': sha(RELEASE / 'xlang3.exe'),
                                        'dll': sha(RELEASE / 'xlang3_runtime.dll')},
              'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'phases': [], 'idle_guards': []}
    output = DATA / (PREFIX + '.json')
    def save():
        output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    def idle(label):
        rows = json.loads(subprocess.check_output(['powershell', '-NoProfile', '-Command',
            'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress']).decode('utf-8-sig') or '[]')
        if isinstance(rows, dict): rows = [rows]
        tools = {'cl.exe', 'link.exe', 'ninja.exe', 'msbuild.exe', 'cmake.exe', 'ctest.exe',
                 'nmake.exe', 'lld-link.exe', 'clang-cl.exe'}
        busy = [r for r in rows if r['ProcessId'] != os.getpid() and
                (r['Name'].lower() in tools or r['Name'].lower().startswith(('python', 'xlang3')))]
        record['idle_guards'].append({'phase': label, 'busy': busy})
        save()
        assert not busy, busy
    env = os.environ.copy()
    env.pop('PYTHONOPTIMIZE', None)
    env.update(XLANG3_PYTHON_LIB='C:/Python/Python314/Lib',
               PYTHONPATH=str(ROOT / 'benchmarks/diagnostics/pyperf_compat'),
               PYTHONIOENCODING='utf-8', PYTHONUNBUFFERED='1')
    try:
        save()
        for name, case in [('sorted7', 'sorted_key_scoped_entry'),
                           ('iteration4', 'sorted_key_iteration_owner'),
                           ('nested1', 'sorted_key_nested_handled_context'),
                           ('canonical10', 'ordinary_canonical_slot_constructor'),
                           ('fallback3', 'explicit_slot_descriptor_fallback'),
                           ('ownership2', 'synchronous_class_argument_lifetime'),
                           ('owner2', 'slot_descriptor_owner'),
                           ('namespace4', 'class_namespace_lifetime'),
                           ('profile3', 'nested_profile_setting'),
                           ('annotation2', 'class_method_annotation_capture'), ('threading', 'threading_runtime_edges'), ('module_binding6', 'call_module_global_binding'), ('integer_sort7', 'sorted_exact_integer_keys'), ('cpp', None)]:
            idle(name)
            assert verify()
            command = [str(RELEASE / ('xlang3.exe' if case else 'xlang3_interpreter_tests.exe'))]
            if case: command.append(str(ROOT / ('tests/fixtures/core/' + case + '.py')))
            phase = {'name': name, 'command': command, 'timeout': False, 'timeout_seconds': 120}
            record['phases'].append(phase)
            try:
                result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, timeout=120)
                phase['exit_code'] = result.returncode
                stdout, stderr = result.stdout, result.stderr
            except subprocess.TimeoutExpired as error:
                phase.update(timeout=True, exit_code=None)
                stdout, stderr = error.stdout or b'', error.stderr or b''
            for stream, raw in [('stdout', stdout), ('stderr', stderr)]:
                path = DATA / (PREFIX + '-' + name + '.' + stream + '.log')
                path.write_bytes(raw)
                phase[stream + '_log'] = path.name
                phase[stream + '_sha256'] = sha(path)
            if case:
                expected = ROOT / ('tests/fixtures/expected/' + case + '.out')
                phase['output_matches_expected'] = stdout.replace(b'\r\n', b'\n') == expected.read_bytes().replace(b'\r\n', b'\n')
            phase['passed'] = phase['exit_code'] == 0 and not phase['timeout'] and not stderr and phase.get('output_matches_expected', True)
            save()
            assert phase['passed'], name
            assert verify()
        idle('after-targeted')
        record['status'] = 'targeted_correctness_passed'
    except BaseException as error:
        record.update(status='targeted_correctness_failed', error=repr(error))
    finally:
        record.update(terminal=True, hashes_unchanged=verify(),
                      completed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
        save()
    print(record['status'], record['candidate_binary_sha256'], flush=True)
    return 0 if record['status'] == 'targeted_correctness_passed' and record['hashes_unchanged'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
