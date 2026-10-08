"""Public layout eligibility and existing boundary correctness; root launches only."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CONTROL = ROOT / 'build-repro/controls/sorted-exact-int-s8-validated-checkpoint-20261008'
CONTROL_SHA = '67449b0b9ecd5a1b4c9669acef85d8b89eb28c7b31df60b7a4653ad5a7c79b56'
BASE = DATA / 'sorted-exact-int-s8-registered-source-20261008.json'
BASE_SHA = 'f20c304e32b0874c929d22dc72018cbd6d5fd9d124844c1d66d8edcc6a231556'
VALIDATION = DATA / 'sorted-exact-int-s8-full-validation-20261008.json'
VALIDATION_SHA = 'd4b028b1255f23b11e664bd9e6778b808a7d160d3628e17f39710faa1900d022'
PROOF = ROOT / 'scratch/performance/native-python-entry-immutable-layout-proposed-20261008-provenance.json'
PROOF_SHA = 'bde7e4fa9b891e60eef8886107675df34c0e6905b94259c59a6c2574b7e2e797'
PATCH = ROOT / 'scratch/performance/native-python-entry-immutable-layout-proposed-20261008.patch'
PATCH_SHA = 'f5a1cf0588369abe0ed15e007dbf34fb101eafabca6d50ac08b4df4a83950e83'
PHASES = [('cpp', None), ('sorted7', 'sorted_key_scoped_entry'),
    ('iteration4', 'sorted_key_iteration_owner'), ('nested1', 'sorted_key_nested_handled_context'),
    ('canonical10', 'ordinary_canonical_slot_constructor'), ('fallback3', 'explicit_slot_descriptor_fallback'),
    ('ownership2', 'synchronous_class_argument_lifetime'), ('owner2', 'slot_descriptor_owner'),
    ('namespace4', 'class_namespace_lifetime'), ('profile3', 'nested_profile_setting'),
    ('annotation2', 'class_method_annotation_capture'), ('threading', 'threading_runtime_edges'),
    ('module_binding6', 'call_module_global_binding'), ('integer_sort7', 'sorted_exact_integer_keys'),
    ('generator_resume', 'generator_resume_state_reuse'), ('debug_frames', 'debug_frame_source_edges'),
    ('monitoring', 'sys_monitoring_all_events')]
SHA = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
READ = lambda p: json.loads(Path(p).read_bytes())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-inventory', type=Path, required=True)
    parser.add_argument('--source-inventory-sha256', required=True)
    parser.add_argument('--build-log', type=Path, required=True)
    parser.add_argument('--build-log-sha256', required=True)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    if sys.implementation.name != 'cpython' or sys.version_info[:3] != (3, 14, 7) or sys.flags.optimize:
        raise RuntimeError('Use unoptimized CPython 3.14.7 only')
    assert Path(sys.executable).resolve() == CP.resolve()
    assert re.fullmatch(r'[a-z0-9-]+', args.prefix) and not any(DATA.glob(args.prefix + '*'))
    pins = {}
    def pin(path, expected=None):
        path = Path(path).resolve(strict=True); value = SHA(path)
        assert expected is None or value == expected, str(path)
        assert str(path) not in pins or pins[str(path)] == value
        pins[str(path)] = value; return value
    for path, expected in [(BASE, BASE_SHA), (VALIDATION, VALIDATION_SHA), (PROOF, PROOF_SHA),
        (PATCH, PATCH_SHA), (CONTROL / 'preserved-release-provenance.json', CONTROL_SHA),
        (args.source_inventory, args.source_inventory_sha256), (args.build_log, args.build_log_sha256)]:
        pin(path, expected)
    base, proof, inventory, control = READ(BASE), READ(PROOF), READ(args.source_inventory), READ(CONTROL / 'preserved-release-provenance.json')
    expected_sources = dict(base['source_sha256'], **proof['candidate_source_sha256'])
    assert inventory['source_sha256'] == expected_sources and len(expected_sources) == 111
    assert control['source_count'] == 110 and base['source_sha256'] == control['source_snapshot_sha256']
    assert control['file_count'] == len(control['files_sha256']) == 178
    prior = READ(VALIDATION)
    assert prior['terminal'] and prior['status'] == 'validated' and prior['hashes_unchanged'] and prior['full_validated']
    release_map = lambda: {p.relative_to(ROOT).as_posix(): SHA(p) for p in sorted(RELEASE.rglob('*')) if p.is_file()}
    binaries = release_map(); assert len(binaries) == 178
    for name in ('xlang3_runtime.dll', 'xlang3_interpreter_tests.exe'):
        assert SHA(RELEASE / name) != control['files_sha256'][name], 'Reject stale S8 runtime/CPP binaries'
    for path, value in {**expected_sources, **binaries}.items(): pin(ROOT / path, value)
    for base_path, values in [(CONTROL, control['files_sha256']), (CONTROL / 'source-snapshot', control['source_snapshot_sha256'])]:
        for path, value in values.items(): pin(base_path / path, value)
    pin(CP, '4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9')
    pin(CP.parent / 'python314.dll', '0f9857ffdfe010fe6b99328d58c2e3c7472ce75f336bf9c2ad9bd5bca3bce700')
    pin(ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py', '2ffc217fe20497156bfcb1cebc7fb87203f33c6d4d3dbb6ecbcbcf91ea4cb317')
    for _, case in PHASES:
        if case:
            pin(ROOT / ('tests/fixtures/core/' + case + '.py'))
            pin(ROOT / ('tests/fixtures/expected/' + case + '.out'))
    pin(__file__)
    record = dict(status='running_targeted_correctness', terminal=False, full_validated=False,
        source_inventory_sha256=args.source_inventory_sha256, source_sha256=expected_sources,
        binaries_sha256=binaries, build_log=str(args.build_log.resolve()), build_log_sha256=args.build_log_sha256,
        controller_sha256=SHA(__file__), candidate_binary_sha256=dict(exe=SHA(RELEASE / 'xlang3.exe'), dll=SHA(RELEASE / 'xlang3_runtime.dll')),
        layout_eligibility='registered public CPP checks actual immutable metadata, prepared restores, code copies/mutation and owners',
        scope='Seventeen fresh targeted phases only; no gate/full/benchmark acceptance',
        phases=[], idle_guards=[], hashes_before=dict(pins), started_utc=datetime.now(timezone.utc).isoformat())
    output = DATA / (args.prefix + '.json')
    def save(): output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))
    def control_maps():
        snapshot = CONTROL / 'source-snapshot'
        files = {p.relative_to(CONTROL).as_posix(): SHA(p) for p in CONTROL.rglob('*')
            if p.is_file() and p != CONTROL / 'preserved-release-provenance.json' and not p.is_relative_to(snapshot)}
        sources = {p.relative_to(snapshot).as_posix(): SHA(p) for p in snapshot.rglob('*') if p.is_file()}
        return files, sources
    def stable():
        return release_map() == binaries and control_maps() == (control['files_sha256'], control['source_snapshot_sha256']) and all(Path(p).is_file() and SHA(p) == h for p, h in pins.items())
    def idle(label):
        rows = json.loads(subprocess.check_output(['powershell', '-NoProfile', '-Command',
            'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress']).decode('utf-8-sig') or '[]')
        if isinstance(rows, dict): rows = [rows]
        busy = [r for r in rows if r['ProcessId'] != os.getpid() and
            (r['Name'].lower().startswith(('python', 'xlang3')) or r['Name'].lower() in
             {'cl.exe','link.exe','ninja.exe','cmake.exe','ctest.exe','msbuild.exe','nmake.exe','clang-cl.exe','lld-link.exe'})]
        record['idle_guards'].append(dict(phase=label, busy=busy)); save(); assert not busy, busy
    environment = dict(os.environ, XLANG3_PYTHON_LIB=str(CP.parent / 'Lib'),
        PYTHONPATH=str(ROOT / 'benchmarks/diagnostics/pyperf_compat'), PYTHONIOENCODING='utf-8', PYTHONUNBUFFERED='1')
    environment.pop('PYTHONOPTIMIZE', None)
    try:
        assert stable()
        for name, case in PHASES:
            idle('before-' + name); assert stable()
            cache = ROOT / 'scratch/performance' / ('pycache-' + args.prefix + '-' + name)
            assert not cache.exists()
            command = [str(RELEASE / ('xlang3.exe' if case else 'xlang3_interpreter_tests.exe'))]
            if case: command.append(str(ROOT / ('tests/fixtures/core/' + case + '.py')))
            row = dict(name=name, command=command, timeout=False, passed=False, timeout_seconds=120)
            record['phases'].append(row); save(); child = None
            stdout = DATA / (args.prefix + '-' + name + '.stdout.log'); stderr = DATA / (args.prefix + '-' + name + '.stderr.log')
            try:
                with stdout.open('xb') as out, stderr.open('xb') as err:
                    child = subprocess.Popen(command, cwd=ROOT, env=dict(environment, PYTHONPYCACHEPREFIX=str(cache)),
                        stdin=subprocess.DEVNULL, stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
                    row['pid'] = child.pid; save()
                    try: row['exit_code'] = child.wait(timeout=120)
                    except subprocess.TimeoutExpired:
                        row['timeout'] = True
                        subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)], capture_output=True, timeout=15)
                        if child.poll() is None: child.kill()
                        row['exit_code'] = child.wait(timeout=15)
                if case:
                    row['output_matches_expected'] = stdout.read_bytes().replace(b'\r\n', b'\n') == (ROOT / ('tests/fixtures/expected/' + case + '.out')).read_bytes().replace(b'\r\n', b'\n')
                row['passed'] = row['exit_code'] == 0 and not row['timeout'] and stderr.read_bytes() == b'' and row.get('output_matches_expected', True)
            finally:
                if child is not None and child.poll() is None:
                    subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)], capture_output=True, timeout=15)
                    if child.poll() is None: child.kill()
                    child.wait(timeout=15)
                for stream, path in [('stdout', stdout), ('stderr', stderr)]:
                    row[stream + '_log'] = path.name; row[stream + '_sha256'] = SHA(path) if path.is_file() else None
                save()
            idle('after-' + name); assert stable() and row['passed'], 'Retain failed phase; no retry'
            print('layout focused:', name, 'PASS', flush=True)
        record['status'] = 'targeted_correctness_passed'
    except BaseException as error: record.update(status='targeted_correctness_failed', error=type(error).__name__ + ': ' + str(error))
    finally:
        record['hashes_after'] = {p: SHA(p) if Path(p).is_file() else None for p in pins}
        record['hashes_unchanged'] = record['hashes_after'] == pins and release_map() == binaries
        if not record['hashes_unchanged']: record['status'] = 'terminal_invalid_hash_drift'
        record.update(terminal=True, completed_utc=datetime.now(timezone.utc).isoformat()); save()
    return 0 if record['status'] == 'targeted_correctness_passed' else 1

if __name__ == '__main__': raise SystemExit(main())
