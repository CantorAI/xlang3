"""Root-only unscored CP/S8 isolated numeric prerequisites; no timing or live changes.

Two exact slices of the original strict numeric groups run once each per runtime. MSBuild Name/PID rows
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
PARENT_MANAGER = ROOT / 'scratch/performance/capture-frame-locals-retained-cpython3147-s8-reference-proposed-20261008.py'
PARENT_MANAGER_SHA = 'ddaf17c09f6460ef28845169a587ddce3f9c5a6cf6fc9e6183f9a4ec22c46ec9'
PARENT_REFERENCE = DATA / 'dict-lazy-index-prerequisites-cpython3147-s8-reference-20261008.json'
PARENT_REFERENCE_SHA = '2266464a276cf086c990441426b8f3f2de6173828e9f1bc2e9c0f2c590d8692e'
ORIGINAL_SOURCE = ROOT / 'scratch/performance/dict-lazy-index-prerequisites-fixture-proposed-20261008.py'
ORIGINAL_SOURCE_SHA = 'd26e25f1ad8bcc2c643031dd67e250d43f27dd72dee34595b20aeb1ab1221c8d'
ORIGINAL_EXPECTED = ROOT / 'scratch/performance/dict-lazy-index-prerequisites-expected-proposed-20261008.out'
ORIGINAL_EXPECTED_SHA = '364a27ba20ecd350f136fbb3694004517081484cf374b71f10454f183b319f25'
PROOF = ROOT / 'scratch/performance/dict-numeric-prerequisites-isolation-provenance-proposed-20261008.json'
PROOF_SHA = '344c42dc2987161eab830b2a41077a17b59c36d05b844034047c82995dc96769'
FIXTURES = (
    ('equivalent', 'scratch/performance/dict-equivalent-numeric-keys-isolated-proposed-20261008.py',
     '68b60af33ad30dd54cd50030444d40fcd135e7cb48bef582a10acbedf8690a3f',
     'scratch/performance/dict-equivalent-numeric-keys-isolated-expected-proposed-20261008.out',
     '8212a05ac357300f8f56a74e9757e09886db23c4d5f2e404487dce337835e0fc', 2),
    ('subclass', 'scratch/performance/dict-numeric-subclass-equality-isolated-proposed-20261008.py',
     '862049741c3e5191368b91c7ca0761d7ae1d862fc2d555450d1180eebecb5aea',
     'scratch/performance/dict-numeric-subclass-equality-isolated-expected-proposed-20261008.out',
     '20f1cfc11e25e59188e0c3103676dfe6cd267dc08706cf3a34c16acc0ba9acb1', 1))
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
        (PARENT_REFERENCE, PARENT_REFERENCE_SHA), (ORIGINAL_SOURCE, ORIGINAL_SOURCE_SHA), (ORIGINAL_EXPECTED, ORIGINAL_EXPECTED_SHA), (PROOF, PROOF_SHA)):
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
    assert proof['parent_inventory_sha256'] == BASE_SHA and proof['original_reference_sha256'] == PARENT_REFERENCE_SHA
    assert proof['raw_before_sha256'] == source_map and proof['original_full_fixture_or_receipt_modified'] is False
    original_text = ORIGINAL_SOURCE.read_bytes().decode('utf-8')
    original_output = ORIGINAL_EXPECTED.read_bytes().decode('utf-8')
    parent_reference = READ(PARENT_REFERENCE)
    assert parent_reference['terminal'] and parent_reference['hashes_unchanged']
    assert parent_reference['source_sha256'] == ORIGINAL_SOURCE_SHA and parent_reference['expected_sha256'] == ORIGINAL_EXPECTED_SHA
    cp_original = [row for row in parent_reference['phases'] if row['runtime'] == 'cpython3147']
    assert len(cp_original) == 1 and cp_original[0]['passed'] and cp_original[0]['output_matches_expected']
    for path, value in source_map.items(): pin(ROOT / path, value)
    assert len(proof['exact_body_slices']) == len(FIXTURES) == 2
    for slice_record, (name, source, source_sha, expected, expected_sha, groups) in zip(proof['exact_body_slices'], FIXTURES):
        assert slice_record['name'] == name and slice_record['source_sha256'] == source_sha
        assert slice_record['expected_sha256'] == expected_sha and slice_record['groups_expected'] == groups
        pin(ROOT / source, source_sha); pin(ROOT / expected, expected_sha)
        start, length = slice_record['original_character_offset'], slice_record['original_character_length']
        assert original_text[start:start + length] == (ROOT / source).read_bytes().decode('utf-8')
        start, length = slice_record['output_offset'], slice_record['output_length']
        assert original_output[start:start + length] == (ROOT / expected).read_bytes().decode('utf-8')
        assert len((ROOT / expected).read_bytes().splitlines()) == groups
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
    record = dict(status='preflight_strict_isolated_numeric_references', terminal=False, passed=False,
        scored=False, benchmark_executed=False, timing_activity_admission=False,
        correctness_activity_policy='Executed ad44 guards: log MSBuild Name/PID only for untimed correctness; other compiler/runtime guards unchanged',
        fixtures=[dict(name=name, source=source, source_sha256=source_sha,
            expected=expected, expected_sha256=expected_sha, groups_expected=groups)
            for name, source, source_sha, expected, expected_sha, groups in FIXTURES],
        isolation_provenance_sha256=PROOF_SHA, original_fixture_sha256=ORIGINAL_SOURCE_SHA, original_expected_sha256=ORIGINAL_EXPECTED_SHA, control_manifest_sha256=CONTROL_SHA,
        restoration_receipt_sha256=WITHDRAWAL_SHA, controller_sha256=SHA(__file__),
        parent_controller_sha256=PARENT_MANAGER_SHA, parent_reference_sha256=PARENT_REFERENCE_SHA,
        current_source_inventory=str(BASE), current_source_inventory_sha256=BASE_SHA,
        current_source_sha256=source_map, current_binaries_sha256=binaries,
        cpython_version=sys.version, cpython_executable=str(CP), cpython_executable_sha256=SHA(CP),
        cpython_dll_sha256=SHA(CP.with_name('python314.dll')), phases=[], idle_guards=[],
        tracked_sha256_before=dict(pins), partial_streams_preserved=True,
        scope='Two CP3147 and two preserved S8 source-isolated numeric correctness children, serialized and each once; not a retry/rescue of original226 failure; no timing/engine application',
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
            for name, source, source_sha, expected, expected_sha, groups in FIXTURES:
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
                    expected_sha256=expected_sha, groups_expected=groups, command=command, timeout_seconds=120, timeout=False,
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
