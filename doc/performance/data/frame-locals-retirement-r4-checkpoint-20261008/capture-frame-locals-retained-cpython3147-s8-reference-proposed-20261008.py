"""Root-only unscored CP/S8 retained-locals references; no timing or live changes.

Two unchanged strict fixtures run once each per runtime. MSBuild Name/PID rows
are logged only for these untimed checks; compiler/runtime children still block.
Preserved S8 failures are captured independently, never used as expected output.
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
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CONTROL = ROOT / 'build-repro/controls/sorted-exact-int-s8-validated-checkpoint-20261008'
CONTROL_SHA = '67449b0b9ecd5a1b4c9669acef85d8b89eb28c7b31df60b7a4653ad5a7c79b56'
BASE = DATA / 'sorted-exact-int-s8-registered-source-20261008.json'
BASE_SHA = 'f20c304e32b0874c929d22dc72018cbd6d5fd9d124844c1d66d8edcc6a231556'
WITHDRAWAL = DATA / 'callee-module-owner-r10-withdrawn-unmeasured-duplicate-restored-s8-20261008.json'
WITHDRAWAL_SHA = '876cac9e56405c9566aa29a7419d4a6acf2cc9ab6f83f6d3190ea6a4d38f25eb'
PARENT_MANAGER = ROOT / 'scratch/performance/capture-runtime-frame-context-cpython3147-s8-correctness-r2-proposed-20261008.py'
PARENT_MANAGER_SHA = 'ad44cade810cecdd1fbc7e567d5fbb9a67b905d247e857713e2b463e1a099b8d'
PARENT_REFERENCE = DATA / 'runtime-frame-context-cpython3147-s8-correctness-r2-20261008.json'
PARENT_REFERENCE_SHA = '159f7294d3ff4e1395722145e3bc8ef72a6ed47834989798cd588af0b5af7d41'
STEM = ROOT / 'scratch/performance/frame-locals-retirement-r4-proposed-20261008'
PATCH = Path(str(STEM) + '.patch')
PATCH_SHA = 'bbc445eb1ee76b150693ee60446abfb1e846f5a8117b7572bfed326f660fc99d'
PROOF = Path(str(STEM) + '-provenance.json')
PROOF_SHA = '24b1c58347f3bea0c4bc40d5e450efb04c8ff614f4c0b482cc12511380cf8e76'
FIXTURES = (
    ('retained', 'scratch/performance/frame-locals-retained-mapping-fixture-proposed-20261008.py',
     '9cf97667d8ef24562c857f651bfd8d5489281e6fd3887f888995a63f256839df',
     'scratch/performance/frame-locals-retained-mapping-expected-proposed-20261008.out',
     '3498e5609c21461d63e16be4273c358cac309f47c59918e0632b95c2c7914150'),
    ('extra', 'scratch/performance/frame-locals-retained-extra-fixture-proposed-20261008.py',
     'ec47ae1e050c1c367125466834185d5cd9edb0b002ad9b03bac1213ae7686a88',
     'scratch/performance/frame-locals-retained-extra-expected-proposed-20261008.out',
     'bcc83f86c18cc6060a085ea6736038efcb31a42bf2830f71fdb4f64e6eec3e71'))
SHA = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
READ = lambda path: json.loads(Path(path).read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
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
        (WITHDRAWAL, WITHDRAWAL_SHA), (PARENT_MANAGER, PARENT_MANAGER_SHA),
        (PARENT_REFERENCE, PARENT_REFERENCE_SHA), (PATCH, PATCH_SHA), (PROOF, PROOF_SHA)):
        pin(path, value)
    pin(__file__)
    control, base, proof, withdrawal = map(READ,
        (CONTROL / 'preserved-release-provenance.json', BASE, PROOF, WITHDRAWAL))
    assert withdrawal['terminal'] and withdrawal['status'] == 'withdrawn_unmeasured_duplicate_r10_preserved_exact_s8_restored'
    assert withdrawal['withdrawal']['r10_timed_children'] == 0
    source_map = base['source_sha256']
    assert control['source_snapshot_sha256'] == source_map
    assert control['source_count'] == len(source_map) == proof['parent_source_count'] == 110
    assert control['file_count'] == len(control['files_sha256']) == 178
    assert proof['parent_inventory_sha256'] == BASE_SHA and proof['patch_sha256'] == PATCH_SHA
    assert proof['raw_before_sha256'] == source_map and proof['restoration_receipt_sha256'] == WITHDRAWAL_SHA
    for path, value in proof['candidate_source_sha256'].items():
        pin(ROOT / proof['candidate_root'] / path, value)
    for path, value in source_map.items():
        pin(ROOT / proof['raw_input_root'] / path, value)
        pin(ROOT / path, value)
    for _, source, source_sha, expected, expected_sha in FIXTURES:
        pin(ROOT / source, source_sha); pin(ROOT / expected, expected_sha)
        assert len((ROOT / expected).read_bytes().splitlines()) == 1
    release_map = lambda: {path.relative_to(ROOT).as_posix(): SHA(path)
        for path in sorted(RELEASE.rglob('*')) if path.is_file()}
    binaries = release_map()
    assert len(binaries) == 178 and binaries == {
        RELEASE.relative_to(ROOT).as_posix() + '/' + path: value
        for path, value in control['files_sha256'].items()}
    for path, value in binaries.items(): pin(ROOT / path, value)
    for directory, values in ((CONTROL, control['files_sha256']),
        (CONTROL / 'source-snapshot', control['source_snapshot_sha256'])):
        for path, value in values.items(): pin(directory / path, value)

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
    record = dict(status='preflight_strict_retained_locals_references', terminal=False, passed=False,
        scored=False, benchmark_executed=False, timing_activity_admission=False,
        correctness_activity_policy='Executed ad44 guards: log MSBuild Name/PID only for untimed correctness; other compiler/runtime guards unchanged',
        fixtures=[dict(name=name, source=source, source_sha256=source_sha,
            expected=expected, expected_sha256=expected_sha, groups_expected=1)
            for name, source, source_sha, expected, expected_sha in FIXTURES],
        patch_sha256=PATCH_SHA, provenance_sha256=PROOF_SHA, control_manifest_sha256=CONTROL_SHA,
        restoration_receipt_sha256=WITHDRAWAL_SHA, controller_sha256=SHA(__file__),
        parent_controller_sha256=PARENT_MANAGER_SHA, parent_reference_sha256=PARENT_REFERENCE_SHA,
        current_source_inventory=str(BASE), current_source_inventory_sha256=BASE_SHA,
        current_source_sha256=source_map, current_binaries_sha256=binaries,
        cpython_version=sys.version, cpython_executable=str(CP), cpython_executable_sha256=SHA(CP),
        cpython_dll_sha256=SHA(CP.with_name('python314.dll')), phases=[], idle_guards=[],
        tracked_sha256_before=dict(pins), partial_streams_preserved=True,
        scope='Two CP3147 and two preserved S8 strict correctness children, serialized and each once; no timing/engine application',
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
            for name, source, source_sha, expected, expected_sha in FIXTURES:
                label = runtime + '-' + name
                idle('before-' + label); assert stable()
                cache = ROOT / 'scratch/performance' / ('pycache-' + args.prefix + '-' + label)
                assert not cache.exists()
                bootstrap = ('import runpy,sys; assert sys.implementation.name == "cpython" and '
                    'sys.version_info[:3] == (3,14,7) and sys.flags.optimize == 0; '
                    'sys.pycache_prefix=sys.argv[2]; runpy.run_path(sys.argv[1],run_name="__main__")')
                command = [str(executable), '-I', '-u', '-X', 'faulthandler', '-c', bootstrap, str(ROOT / source), str(cache)] \
                    if runtime == 'cpython3147' else [str(executable), str(ROOT / source)]
                stdout = DATA / (args.prefix + '-' + label + '.stdout.log')
                stderr = DATA / (args.prefix + '-' + label + '.stderr.log')
                row = dict(runtime=runtime, fixture=name, source_sha256=source_sha,
                    expected_sha256=expected_sha, command=command, timeout_seconds=120, timeout=False,
                    passed=False, stdout_log=stdout.name, stderr_log=stderr.name)
                record['phases'].append(row); save(); child = None
                print('Starting', label, flush=True)
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
                    row['output_matches_expected'] = stdout.read_bytes().replace(b'\r\n', b'\n') == (ROOT / expected).read_bytes().replace(b'\r\n', b'\n')
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
                idle('after-' + label)
                assert stable(), 'Retain raw child evidence; refuse further children after hash drift'
                assert not row.get('cleanup_error'), 'Refuse further children with unresolved cleanup'
                if runtime == 'cpython3147':
                    assert row['passed'], 'Strict CP expected output is never invented or replaced'
                print('Terminal', label, 'passed', row['passed'], flush=True)
        assert len(record['phases']) == 4
        record.update(cpython_references_passed=all(row['passed'] for row in record['phases'] if row['runtime'] == 'cpython3147'),
            preserved_s8_all_passed=all(row['passed'] for row in record['phases'] if row['runtime'] == 'preserved_s8'))
        record['passed'] = all(row['passed'] for row in record['phases'])
        record['status'] = 'terminal_correctness_reference_capture_complete' if record['passed'] else 'terminal_correctness_reference_capture_complete_with_s8_failures'
    except BaseException as error:
        record.update(status='terminal_strict_correctness_reference_failed', error=type(error).__name__ + ': ' + str(error))
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
