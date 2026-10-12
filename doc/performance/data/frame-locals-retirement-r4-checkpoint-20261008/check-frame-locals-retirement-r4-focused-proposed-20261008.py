"""Root-only fresh frame-retirement correctness checks; no gate or benchmark.

Derived from the reviewed focused runner. All ten children execute at most once,
with strict original output and a 120-second cap; the first failure stops work.
The actual successful build command/log are pinned from root's terminal receipt.
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
BASELINE = ROOT / 'build-repro/Release'
CONTROL = ROOT / 'build-repro/controls/sorted-exact-int-s8-validated-checkpoint-20261008'
CONTROL_SHA = '67449b0b9ecd5a1b4c9669acef85d8b89eb28c7b31df60b7a4653ad5a7c79b56'
BASE = DATA / 'sorted-exact-int-s8-registered-source-20261008.json'
BASE_SHA = 'f20c304e32b0874c929d22dc72018cbd6d5fd9d124844c1d66d8edcc6a231556'
APPLICATION = DATA / 'frame-locals-retirement-s8-application-20261008-applied-source.json'
APPLICATION_SHA = 'dc563e2b363dc3992a2c8b323e0f59e953749a715bb4ec97264ef1fa846ab153'
PRESERVED_SHA = '29841491e3a6e934db9ef19dfdd01ef3ef71ab14e2cc3333b80213d793ab9e26'
PROOF = ROOT / 'scratch/performance/frame-locals-retirement-r4-proposed-20261008-provenance.json'
PROOF_SHA = '24b1c58347f3bea0c4bc40d5e450efb04c8ff614f4c0b482cc12511380cf8e76'
PATCH = ROOT / 'scratch/performance/frame-locals-retirement-r4-proposed-20261008.patch'
PATCH_SHA = 'bbc445eb1ee76b150693ee60446abfb1e846f5a8117b7572bfed326f660fc99d'
TEMPLATE = ROOT / 'scratch/performance/check-callee-module-owner-r10-focused-r2-20261008.py'
TEMPLATE_SHA = '781eaf174490b139fe61b234f1ed2c0e183868f34706d164de7ad3fea76b98f1'
BUILD = DATA / 'frame-locals-retirement-r4-build-terminal-20261008.json'
BUILD_SHA = 'df8ba2f7b324b6b29173e7622f47fbaa6a8c9ea86560e24db2557b1e8c93ae0a'
BUILD_WRAPPER = ROOT / 'scratch/performance/build-frame-locals-retirement-r4-20261008.cmd'
BUILD_WRAPPER_SHA = '2856a414d0f760757daba4b880953bd5bc4b68dd9ed51dc17b6624d6ae1a70ef'
BUILD_LOG = DATA / 'frame-locals-retirement-r4-build-20261008.log'
BUILD_LOG_SHA = 'd6f11dc2d3457b547d6d9bfa1435776801ea7da21f4665f706ee14ab739f16b1'
BUILD_ARGV = ['cmd.exe', '/d', '/c', 'scratch\\performance\\build-frame-locals-retirement-r4-20261008.cmd']
REFERENCES = (
    ('runtime-frame-context-cpython3147-s8-correctness-r2-20261008.json',
     '159f7294d3ff4e1395722145e3bc8ef72a6ed47834989798cd588af0b5af7d41', 1),
    ('frame-locals-retained-cpython3147-s8-reference-20261008.json',
     '71a0288e97ffb357de8e447a7a9fca38450fb173201b708988d74341d7150751', 2))
# name, original source, original expected output, source SHA, expected SHA
PHASES = [
    ('cpp', None, None, None, None),
    ('framecontext4', 'scratch/performance/runtime-frame-context-coalesced-fixture-proposed-20261008.py',
     'scratch/performance/runtime-frame-context-coalesced-expected-proposed-20261008.out',
     'fd942dfb3981c01b6464f623ebb1b37640772c532b18ad7381abebf2d9e5ffc7',
     '97d5a6c2764deb48e4f217f09d148f13b4f8332d88b6f930f14ba12ede9e065d'),
    ('retained1', 'scratch/performance/frame-locals-retained-mapping-fixture-proposed-20261008.py',
     'scratch/performance/frame-locals-retained-mapping-expected-proposed-20261008.out',
     '9cf97667d8ef24562c857f651bfd8d5489281e6fd3887f888995a63f256839df',
     '3498e5609c21461d63e16be4273c358cac309f47c59918e0632b95c2c7914150'),
    ('extra1', 'scratch/performance/frame-locals-retained-extra-fixture-proposed-20261008.py',
     'scratch/performance/frame-locals-retained-extra-expected-proposed-20261008.out',
     'ec47ae1e050c1c367125466834185d5cd9edb0b002ad9b03bac1213ae7686a88',
     'bcc83f86c18cc6060a085ea6736038efcb31a42bf2830f71fdb4f64e6eec3e71'),
    ('debug_frames', 'tests/fixtures/core/debug_frame_source_edges.py', 'tests/fixtures/expected/debug_frame_source_edges.out',
     'e978cbaf7d6c3c668a7a22a8dae974735f09b680f282736444e4bd83ef1814f9',
     '228b6a6746487f977e4282ab4029ba916bd0e672ae1f65500e2bae2abf7fb65a'),
    ('profile3', 'tests/fixtures/core/nested_profile_setting.py', 'tests/fixtures/expected/nested_profile_setting.out',
     '9b2e6a38dc339115de0d0399f9fcf1fb8b22cd792bf270d480d42a7a8c8320bf',
     '01ecf4bf1a5a75774e1c2fc10d7c7a969ad0d75af58304f5ee37e9b74bcae532'),
    ('threading', 'tests/fixtures/core/threading_runtime_edges.py', 'tests/fixtures/expected/threading_runtime_edges.out',
     '56016bd84f76782ce736058670c50035c8a1600ac67fca201ef1a304e6875c39',
     '7ac4a64a76bc6207024ea9c001dcfcf336e991effe39b9c08602ceb7c332ba22'),
    ('monitoring', 'tests/fixtures/core/sys_monitoring_all_events.py', 'tests/fixtures/expected/sys_monitoring_all_events.out',
     'eccb2c7dae1b59f2731992b523f4dd612cc17202c0a0c184a5b4077cdbee9503',
     'a49b1025bc458e5d55752a48e80eb36ed43af00ff72e8467b88a408e8a97adc5'),
    ('sorted7', 'tests/fixtures/core/sorted_key_scoped_entry.py', 'tests/fixtures/expected/sorted_key_scoped_entry.out',
     'ccf3922036d85af6f2c654c29046c924b993cb3c1be2b0d65e23fe5fe24f6f26',
     'ba8f18a40977207425881487b5ccde27f432cb5d75a1bde14fc14be9c8798d31'),
    ('nested1', 'tests/fixtures/core/sorted_key_nested_handled_context.py', 'tests/fixtures/expected/sorted_key_nested_handled_context.out',
     'b07f57cd7d8017bd0c9e316aa61d2b53c417d5c7a21ebd32bd14c72e545d5766',
     '92ca4b59af1fa87ef558a34cc96baebbb62cdaaaaac5e97b3d52d4ab88e0967f')]
SHA = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
READ = lambda path: json.loads(Path(path).read_bytes())


def tree(directory):
    return {path.relative_to(directory).as_posix(): SHA(path)
            for path in sorted(directory.rglob('*')) if path.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    assert sys.implementation.name == 'cpython' and sys.version_info[:3] == (3, 14, 7)
    assert not sys.flags.optimize and Path(sys.executable).resolve() == CP.resolve()
    assert ROOT == Path('D:/CantorAI/xlang3').resolve() and Path.cwd().resolve() == ROOT
    assert re.fullmatch(r'[a-z0-9-]+', args.prefix) and not any(DATA.glob(args.prefix + '*'))
    output = DATA / (args.prefix + '.json')
    pins, protected_trees, binaries, sources = {}, {}, {}, {}
    record = dict(status='preflight_targeted_correctness', terminal=False, scored=False,
        full_validated=False, accepted=False, benchmark_executed=False,
        scope='Ten fresh strict frame-retirement correctness children only; no full gate or benchmark claim',
        controller_sha256=SHA(__file__), application_receipt_sha256=APPLICATION_SHA,
        proposal_sha256=PROOF_SHA, patch_sha256=PATCH_SHA, phases=[], idle_guards=[],
        phase_order=[phase[0] for phase in PHASES], unknown_later_phases=[phase[0] for phase in PHASES],
        started_utc=datetime.now(timezone.utc).isoformat())

    def save():
        output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))

    def pin(path, expected=None):
        path = Path(path).resolve(strict=True); value = SHA(path)
        assert expected is None or value == expected, str(path)
        assert str(path) not in pins or pins[str(path)] == value
        pins[str(path)] = value
        return path

    def stable():
        return all(Path(path).is_file() and SHA(path) == value for path, value in pins.items()) and all(
            tree(Path(path)) == values for path, values in protected_trees.items())

    def idle(label):
        item = dict(phase=label, allowed_controller_pid=os.getpid(), busy=[])
        record['idle_guards'].append(item); save()
        try:
            result = subprocess.run(['powershell', '-NoProfile', '-Command',
                'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'],
                check=True, capture_output=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
            rows = json.loads(result.stdout.decode('utf-8-sig') or '[]')
            if isinstance(rows, dict): rows = [rows]
            item['busy'] = [row for row in rows if row['ProcessId'] != os.getpid() and
                (row['Name'].lower().startswith(('python', 'xlang3')) or row['Name'].lower() in
                 {'msbuild.exe', 'cl.exe', 'link.exe', 'ninja.exe', 'cmake.exe', 'ctest.exe',
                  'nmake.exe', 'clang-cl.exe', 'lld-link.exe'})]
            save(); assert not item['busy'], item['busy']
        except BaseException as error:
            item['error'] = type(error).__name__ + ': ' + str(error); save(); raise

    save()
    try:
        for path, value in ((BASE, BASE_SHA), (APPLICATION, APPLICATION_SHA), (PROOF, PROOF_SHA),
            (PATCH, PATCH_SHA), (TEMPLATE, TEMPLATE_SHA), (BUILD, BUILD_SHA),
            (BUILD_WRAPPER, BUILD_WRAPPER_SHA), (BUILD_LOG, BUILD_LOG_SHA),
            (CONTROL / 'preserved-release-provenance.json', CONTROL_SHA),
            (CP, '4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9'),
            (CP.with_name('python314.dll'), '0f9857ffdfe010fe6b99328d58c2e3c7472ce75f336bf9c2ad9bd5bca3bce700')):
            pin(path, value)
        pin(__file__)
        pin(ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py',
            '2ffc217fe20497156bfcb1cebc7fb87203f33c6d4d3dbb6ecbcbcf91ea4cb317')
        build = READ(BUILD)
        assert build['terminal'] and build['status'] == 'build_completed' and build['exit_code'] == 0
        assert build['command'] == BUILD_ARGV and Path(build['working_directory']).resolve() == ROOT
        assert build['wrapper'] == BUILD_WRAPPER.relative_to(ROOT).as_posix() and build['wrapper_sha256'] == BUILD_WRAPPER_SHA
        assert build['build_log'] == BUILD_LOG.relative_to(ROOT).as_posix() and build['build_log_sha256'] == BUILD_LOG_SHA
        assert build['application_receipt_sha256'] == APPLICATION_SHA
        base, application, proof, control = map(READ, (BASE, APPLICATION, PROOF,
            CONTROL / 'preserved-release-provenance.json'))
        assert application['terminal'] and application['status'] == 'applied_unbuilt_unvalidated_frame_locals_retirement_correctness'
        assert application['proposal_sha256'] == PROOF_SHA and application['parent_inventory_sha256'] == BASE_SHA
        assert not application['accepted_gate_baseline_changed']
        assert application['baseline_before'] == application['baseline_after']
        assert len(proof['candidate_source_sha256']) == 3
        sources = dict(base['source_sha256'], **proof['candidate_source_sha256'])
        assert sources == application['source_sha256'] and len(sources) == application['source_count'] == 111
        assert control['source_snapshot_sha256'] == base['source_sha256'] and control['source_count'] == 110
        assert control['file_count'] == len(control['files_sha256']) == 178
        for path, value in sources.items(): pin(ROOT / path, value)
        for path, value in proof['candidate_source_sha256'].items(): pin(ROOT / proof['candidate_root'] / path, value)
        for path, value in base['source_sha256'].items(): pin(ROOT / proof['raw_input_root'] / path, value)
        preserved = Path(application['preserved_parent']).resolve(strict=True)
        assert preserved == (ROOT / 'build-repro/controls/frame-locals-retirement-s8-application-20261008').resolve()
        assert application['preserved_manifest_sha256'] == PRESERVED_SHA
        old = READ(pin(preserved / 'preserved-release-provenance.json', PRESERVED_SHA))
        assert old['files_sha256'] == control['files_sha256'] and old['file_count'] == 178
        assert old['source_snapshot_sha256'] == base['source_sha256'] and old['source_count'] == 110
        assert old['baseline_sha256'] == application['baseline_before']
        assert old['unrelated_dirty_snapshot_sha256'] == application['unrelated_dirty_sha256_before'] == application['unrelated_dirty_sha256_after']
        for directory, values in ((CONTROL, control['files_sha256']),
            (CONTROL / 'source-snapshot', control['source_snapshot_sha256']), (preserved, old['files_sha256']),
            (preserved / 'source-snapshot', old['source_snapshot_sha256']),
            (preserved / 'unrelated-dirty-snapshot', old['unrelated_dirty_snapshot_sha256'])):
            for path, value in values.items(): pin(directory / path, value)
        protected_trees[str(CONTROL)] = dict(control['files_sha256'],
            **{'source-snapshot/' + path: value for path, value in control['source_snapshot_sha256'].items()},
            **{'preserved-release-provenance.json': CONTROL_SHA})
        protected_trees[str(preserved)] = dict(old['files_sha256'],
            **{'source-snapshot/' + path: value for path, value in old['source_snapshot_sha256'].items()},
            **{'unrelated-dirty-snapshot/' + path: value for path, value in old['unrelated_dirty_snapshot_sha256'].items()},
            **{'preserved-release-provenance.json': PRESERVED_SHA})
        assert all(tree(Path(directory)) == values for directory, values in protected_trees.items())
        binaries = tree(RELEASE)
        assert len(binaries) == 178
        baseline = tree(BASELINE)
        assert len(baseline) == 177 and baseline == application['baseline_before']
        for name in ('xlang3_runtime.dll', 'xlang3_interpreter_tests.exe'):
            assert binaries[name] != control['files_sha256'][name], 'Reject stale S8 runtime/CPP binary'
        protected_trees[str(RELEASE)] = binaries; protected_trees[str(BASELINE)] = baseline
        for directory, values in ((RELEASE, binaries), (BASELINE, baseline)):
            for path, value in values.items(): pin(directory / path, value)
        references = []
        for filename, expected_sha, expected_cp_count in REFERENCES:
            reference = READ(pin(DATA / filename, expected_sha))
            assert reference['terminal'] and reference['hashes_unchanged']
            cp_rows = [row for row in reference['phases'] if row['runtime'] == 'cpython3147']
            assert len(cp_rows) == expected_cp_count
            assert all(row['passed'] and row['exit_code'] == 0 and not row['timeout'] and row['output_matches_expected'] for row in cp_rows)
            for row in reference['phases']:
                for stream in ('stdout', 'stderr'):
                    log = DATA / row[stream + '_log']; assert log.parent == DATA
                    pin(log, row[stream + '_sha256'])
            references.append(reference)
        assert references[0]['source_sha256'] == PHASES[1][3] and references[0]['expected_sha256'] == PHASES[1][4]
        assert references[0]['groups_expected'] == 4
        pin(references[0]['source'], PHASES[1][3]); pin(references[0]['expected'], PHASES[1][4])
        for row, phase in zip(references[1]['fixtures'], PHASES[2:4]):
            assert row['source_sha256'] == phase[3] and row['expected_sha256'] == phase[4] and row['groups_expected'] == 1
        for _, source, expected, source_sha, expected_sha in PHASES:
            if source: pin(ROOT / source, source_sha); pin(ROOT / expected, expected_sha)
        record.update(status='running_targeted_correctness', source_inventory_sha256=APPLICATION_SHA,
            source_sha256=sources, source_count=111,
            binaries_sha256={RELEASE.relative_to(ROOT).as_posix() + '/' + key: value for key, value in binaries.items()},
            candidate_binary_sha256=dict(exe=binaries['xlang3.exe'], dll=binaries['xlang3_runtime.dll']),
            baseline_sha256=baseline, build_receipt_sha256=BUILD_SHA,
            build_log=str(BUILD_LOG), build_log_sha256=BUILD_LOG_SHA,
            build_argv=BUILD_ARGV, build_argv_sha256=hashlib.sha256(json.dumps(BUILD_ARGV, separators=(',', ':')).encode('utf-8')).hexdigest(),
            build_wrapper_sha256=BUILD_WRAPPER_SHA, root_recorded_build_exit_code=build['exit_code'], hashes_before=dict(pins))
        environment = dict(os.environ, XLANG3_PYTHON_LIB=str(CP.parent / 'Lib'),
            PYTHONPATH=str(ROOT / 'benchmarks/diagnostics/pyperf_compat'), PYTHONIOENCODING='utf-8', PYTHONUNBUFFERED='1')
        environment.pop('PYTHONOPTIMIZE', None)
        for name, source, expected, source_sha, expected_sha in PHASES:
            idle('before-' + name); assert stable()
            cache = ROOT / 'scratch/performance' / ('pycache-' + args.prefix + '-' + name)
            assert not cache.exists()
            command = [str(RELEASE / ('xlang3.exe' if source else 'xlang3_interpreter_tests.exe'))]
            if source: command.append(str(ROOT / source))
            row = dict(name=name, command=command, timeout=False, passed=False, timeout_seconds=120,
                source=source, source_sha256=source_sha, expected=expected, expected_sha256=expected_sha)
            record['phases'].append(row); record['unknown_later_phases'].remove(name); save(); child = None
            stdout = DATA / (args.prefix + '-' + name + '.stdout.log')
            stderr = DATA / (args.prefix + '-' + name + '.stderr.log')
            try:
                with stdout.open('xb') as out, stderr.open('xb') as err:
                    child = subprocess.Popen(command, cwd=ROOT, env=dict(environment, PYTHONPYCACHEPREFIX=str(cache)),
                        stdin=subprocess.DEVNULL, stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
                    row['pid'] = child.pid; save()
                    try: row['exit_code'] = child.wait(timeout=120)
                    except subprocess.TimeoutExpired:
                        row['timeout'] = True
                        subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=15)
                        if child.poll() is None: child.kill()
                        row['exit_code'] = child.wait(timeout=15)
                if source:
                    row['output_matches_expected'] = stdout.read_bytes().replace(b'\r\n', b'\n') == (ROOT / expected).read_bytes().replace(b'\r\n', b'\n')
                row['passed'] = row['exit_code'] == 0 and not row['timeout'] and stderr.read_bytes() == b'' and row.get('output_matches_expected', True)
            finally:
                if child is not None and child.poll() is None:
                    subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=15)
                    if child.poll() is None: child.kill()
                    child.wait(timeout=15)
                for stream, path in (('stdout', stdout), ('stderr', stderr)):
                    row[stream + '_log'] = path.name; row[stream + '_sha256'] = SHA(path) if path.is_file() else None
                save()
            idle('after-' + name); assert stable() and row['passed'], 'Retain failed phase; no retry'
            print('frame retirement focused:', name, 'PASS', flush=True); save()
        record['status'] = 'targeted_correctness_passed'
    except BaseException as error:
        record.update(status='targeted_correctness_failed', error=type(error).__name__ + ': ' + str(error))
    finally:
        record['hashes_after'] = {path: SHA(path) if Path(path).is_file() else None for path in pins}
        record['hashes_unchanged'] = stable()
        if not record['hashes_unchanged']: record['status'] = 'terminal_invalid_hash_drift'
        record.update(terminal=True, completed_utc=datetime.now(timezone.utc).isoformat()); save()
    return 0 if record['status'] == 'targeted_correctness_passed' else 1


if __name__ == '__main__': raise SystemExit(main())
