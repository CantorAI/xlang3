"""Root-only unscored correctness Python4 CP/S8 references; no timing or live changes.

Run only without concurrent timing/compiler/runtime children. MSBuild name rows are logged for these untimed checks. The current-source inventory is an
explicit caller pin: this manager neither applies R11 nor assumes R10 retained.
"""
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
CONTROL = ROOT / 'build-repro/controls/sorted-exact-int-s8-validated-checkpoint-20261008'
CONTROL_SHA = '67449b0b9ecd5a1b4c9669acef85d8b89eb28c7b31df60b7a4653ad5a7c79b56'
BASE = DATA / 'sorted-exact-int-s8-registered-source-20261008.json'
BASE_SHA = 'f20c304e32b0874c929d22dc72018cbd6d5fd9d124844c1d66d8edcc6a231556'
STEM = ROOT / 'scratch/performance/runtime-frame-context-coalesced-proposed-20261008'
PATCH = Path(str(STEM) + '.patch')
PATCH_SHA = 'c7621f919aa075824a795b4e23dea610c48aa868bff22d45ab9524a9d6c8f2d9'
PROOF = Path(str(STEM) + '-provenance.json')
PROOF_SHA = '2937c29b429f0a9954996cdb767d13b789d5f1f3344bf81ad6e8ef8ec452da9e'
CANDIDATES = Path(str(STEM) + '-candidates')
SOURCE = CANDIDATES / 'tests/fixtures/core/runtime_frame_context.py'
SOURCE_SHA = 'fd942dfb3981c01b6464f623ebb1b37640772c532b18ad7381abebf2d9e5ffc7'
EXPECTED = CANDIDATES / 'tests/fixtures/expected/runtime_frame_context.out'
EXPECTED_SHA = '97d5a6c2764deb48e4f217f09d148f13b4f8332d88b6f930f14ba12ede9e065d'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
SHA = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
READ = lambda path: json.loads(Path(path).read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--current-source-inventory', type=Path, required=True)
    parser.add_argument('--current-source-inventory-sha256', required=True)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    assert sys.implementation.name == 'cpython' and sys.version_info[:3] == (3, 14, 7)
    assert not sys.flags.optimize and Path(sys.executable).resolve() == CP.resolve()
    assert ROOT == Path('D:/CantorAI/xlang3').resolve() and Path.cwd().resolve() == ROOT
    assert re.fullmatch(r'[a-z0-9-]+', args.prefix) and not any(DATA.glob(args.prefix + '*'))
    pins = {}

    def pin(path, expected=None):
        path = Path(path).resolve(strict=True)
        value = SHA(path)
        assert expected is None or value == expected, str(path)
        assert str(path) not in pins or pins[str(path)] == value
        pins[str(path)] = value
        return value

    for path, value in ((CP, '4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9'),
        (CP.with_name('python314.dll'), '0f9857ffdfe010fe6b99328d58c2e3c7472ce75f336bf9c2ad9bd5bca3bce700'),
        (CONTROL / 'preserved-release-provenance.json', CONTROL_SHA), (BASE, BASE_SHA),
        (PATCH, PATCH_SHA), (PROOF, PROOF_SHA), (SOURCE, SOURCE_SHA), (EXPECTED, EXPECTED_SHA),
        (args.current_source_inventory, args.current_source_inventory_sha256)):
        pin(path, value)
    pin(__file__)
    control, base, proof, current = map(READ, (CONTROL / 'preserved-release-provenance.json',
        BASE, PROOF, args.current_source_inventory))
    assert control['source_snapshot_sha256'] == base['source_sha256']
    assert control['source_count'] == len(control['source_snapshot_sha256']) == 110
    assert control['file_count'] == len(control['files_sha256']) == 178
    assert proof['parent_inventory_sha256'] == BASE_SHA and proof['patch_sha256'] == PATCH_SHA
    assert proof['raw_before_sha256'] == base['source_sha256']
    assert len(EXPECTED.read_bytes().splitlines()) == 4
    for path, value in proof['candidate_source_sha256'].items():
        pin(CANDIDATES / path, value)
    for path, value in proof['raw_before_sha256'].items():
        pin(ROOT / proof['raw_input_root'] / path, value)
    source_map = current['source_sha256']
    assert source_map and all(re.fullmatch(r'[0-9a-f]{64}', value) for value in source_map.values())
    for path, value in source_map.items():
        candidate = (ROOT / path).resolve(strict=True)
        assert candidate.is_relative_to(ROOT)
        pin(candidate, value)
    release_map = lambda: {path.relative_to(ROOT).as_posix(): SHA(path)
        for path in sorted(RELEASE.rglob('*')) if path.is_file()}
    binaries = release_map()
    assert len(binaries) == 178
    for path, value in binaries.items():
        pin(ROOT / path, value)
    for directory, values in ((CONTROL, control['files_sha256']),
        (CONTROL / 'source-snapshot', control['source_snapshot_sha256'])):
        for path, value in values.items():
            pin(directory / path, value)

    def control_map():
        snapshot = CONTROL / 'source-snapshot'
        files = {path.relative_to(CONTROL).as_posix(): SHA(path)
            for path in CONTROL.rglob('*') if path.is_file() and
            path != CONTROL / 'preserved-release-provenance.json' and not path.is_relative_to(snapshot)}
        sources = {path.relative_to(snapshot).as_posix(): SHA(path)
            for path in snapshot.rglob('*') if path.is_file()}
        return files, sources

    def stable():
        return release_map() == binaries and control_map() == (
            control['files_sha256'], control['source_snapshot_sha256']) and all(
            Path(path).is_file() and SHA(path) == value for path, value in pins.items())

    output = DATA / (args.prefix + '.json')
    record = dict(status='preflight_strict_python_reference', terminal=False, passed=False,
        scored=False, benchmark_executed=False, timing_activity_admission=False,
        correctness_activity_policy='MSBuild name rows logged and allowed only for unscored correctness; other compiler/runtime guards unchanged; no timing exception',
        source=str(SOURCE), source_sha256=SOURCE_SHA,
        expected=str(EXPECTED), expected_sha256=EXPECTED_SHA, groups_expected=4,
        patch_sha256=PATCH_SHA, provenance_sha256=PROOF_SHA, control_manifest_sha256=CONTROL_SHA,
        controller_sha256=SHA(__file__), current_source_inventory=str(args.current_source_inventory.resolve()),
        current_source_inventory_sha256=args.current_source_inventory_sha256,
        current_source_sha256=source_map, current_binaries_sha256=binaries,
        cpython_version=sys.version, cpython_executable=str(CP), cpython_executable_sha256=SHA(CP),
        cpython_dll_sha256=SHA(CP.with_name('python314.dll')), phases=[], idle_guards=[],
        tracked_sha256_before=dict(pins), partial_streams_preserved=True,
        scope='One CPython and one preserved S8 Python4 correctness child, serialized; no timing/R11 application/R10 acceptance',
        output_comparison='Exact bytes after CRLF to LF normalization only',
        started_utc=datetime.now(timezone.utc).isoformat())

    def save():
        output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))

    def idle(label):
        item = dict(phase=label, allowed_controller_pid=os.getpid())
        try:
            command = 'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'
            completed = subprocess.run(['powershell', '-NoProfile', '-Command', command],
                capture_output=True, check=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
            rows = json.loads(completed.stdout.decode('utf-8-sig') or '[]')
            if isinstance(rows, dict): rows = [rows]
            item['logged_msbuild_workers'] = [row for row in rows if row['ProcessId'] != os.getpid() and row['Name'].lower() == 'msbuild.exe']
            item['busy'] = [row for row in rows if row['ProcessId'] != os.getpid() and
                (row['Name'].lower().startswith(('python', 'xlang3')) or row['Name'].lower() in
                {'cl.exe', 'link.exe', 'ninja.exe', 'cmake.exe', 'ctest.exe',
                 'nmake.exe', 'clang-cl.exe', 'lld-link.exe'})]
        except BaseException as error:
            item['error'] = type(error).__name__ + ': ' + str(error)
        record['idle_guards'].append(item); save()
        assert 'error' not in item and not item['busy'], item

    environment = os.environ.copy()
    for name in ('PYTHONOPTIMIZE', 'PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
        environment.pop(name, None)
    environment.update(XLANG3_PYTHON_LIB=str(CP.parent / 'Lib'), PYTHONIOENCODING='utf-8')
    try:
        assert stable()
        for runtime, executable in (('cpython3147', CP), ('preserved_s8', CONTROL / 'xlang3.exe')):
            idle('before-' + runtime); assert stable()
            cache = ROOT / 'scratch/performance' / ('pycache-' + args.prefix + '-' + runtime)
            assert not cache.exists()
            bootstrap = ('import runpy,sys; assert sys.implementation.name == "cpython" and '
                'sys.version_info[:3] == (3,14,7) and sys.flags.optimize == 0; '
                'sys.pycache_prefix=sys.argv[2]; runpy.run_path(sys.argv[1],run_name="__main__")')
            command = [str(executable), '-I', '-u', '-X', 'faulthandler', '-c', bootstrap, str(SOURCE), str(cache)] \
                if runtime == 'cpython3147' else [str(executable), str(SOURCE)]
            stdout = DATA / (args.prefix + '-' + runtime + '.stdout.log')
            stderr = DATA / (args.prefix + '-' + runtime + '.stderr.log')
            row = dict(runtime=runtime, command=command, timeout_seconds=120, timeout=False,
                passed=False, stdout_log=stdout.name, stderr_log=stderr.name)
            record['phases'].append(row); save(); child = None
            try:
                with stdout.open('xb') as out, stderr.open('xb') as err:
                    child = subprocess.Popen(command, cwd=ROOT,
                        env=dict(environment, PYTHONPYCACHEPREFIX=str(cache)), stdin=subprocess.DEVNULL,
                        stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
                    row['pid'] = child.pid; save()
                    try:
                        row['exit_code'] = child.wait(timeout=120)
                    except subprocess.TimeoutExpired:
                        row['timeout'] = True
                        subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=15)
                        if child.poll() is None: child.kill()
                        row['exit_code'] = child.wait(timeout=15)
                row['output_matches_expected'] = stdout.read_bytes().replace(b'\r\n', b'\n') == EXPECTED.read_bytes().replace(b'\r\n', b'\n')
                row['passed'] = row['exit_code'] == 0 and not row['timeout'] and row['output_matches_expected'] and stderr.read_bytes() == b''
            finally:
                if child is not None and child.poll() is None:
                    try:
                        subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=15)
                        if child.poll() is None: child.kill()
                        child.wait(timeout=15)
                    except BaseException as error:
                        row['cleanup_error'] = type(error).__name__ + ': ' + str(error)
                        row['passed'] = False
                for stream, path in (('stdout', stdout), ('stderr', stderr)):
                    row[stream + '_sha256'] = SHA(path) if path.is_file() else None
                    row[stream + '_bytes'] = path.stat().st_size if path.is_file() else 0
                save()
            idle('after-' + runtime)
            assert row['passed'] and stable(), 'Retain failed child; no retry or replacement'
        record.update(status='terminal_strict_python_references_passed', passed=True)
    except BaseException as error:
        record.update(status='terminal_strict_python_reference_failed', error=type(error).__name__ + ': ' + str(error))
    finally:
        after = {path: SHA(path) if Path(path).is_file() else None for path in pins}
        record.update(tracked_sha256_after=after, hashes_unchanged=after == pins and stable(),
            terminal=True, completed_utc=datetime.now(timezone.utc).isoformat())
        if not record['hashes_unchanged']:
            record.update(status='terminal_invalid_reference_hash_drift', passed=False)
        save()
    print(record['status'], 'passed', record['passed'], flush=True)
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
