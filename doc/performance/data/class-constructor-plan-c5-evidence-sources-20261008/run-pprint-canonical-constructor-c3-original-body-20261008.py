"""One CPython3.14.7 then fixed XLang pprint body per mode; unscored only.

Baseline is the R5 correctness-only checkpoint; candidate is the C3 canonical
constructor/namespace/profile/annotation correction trial. Baseline requires its original sorted7/iteration4/nested1/CPP targeted
receipt. Candidate requires sorted7/iteration4/nested1/canonical10/fallback3/ownership2/owner2/namespace4/profile3/annotation2/CPP
plus current source/Release pins, and the frozen baseline controller's
completed R5 body and its complete preserved control. No gate/full97 prerequisite,
retries, performance acceptance or full-validity claim.
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
EXE = ROOT / 'build-repro/main-verify-20261006/Release/xlang3.exe'
CHILD = ROOT / 'scratch/performance/pprint-original-safe-repr-r2-diagnostic-20261008.py'
CHILD_SHA = '5d57809bfc7437ccb6eef30d54c34b78f87824d2b01644959717cd36a8ea437c'
BENCHMARK = CP.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks/bm_pprint/run_benchmark.py'
BENCHMARK_SHA = '07ef57201c7919aedf9e340c7fb1561a40dfee698818c9694930f3e427091d6d'
PPRINT_SHA = 'c29eb77af95120a7e9b107b6dc3cf093fcbe37b22aa27441cf58cacbeb56904e'
HOOK = ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
HOOK_SHA = '2ffc217fe20497156bfcb1cebc7fb87203f33c6d4d3dbb6ecbcbcf91ea4cb317'
BASELINE_KIND = 'r5-canonical-constructor-control'
BASELINE_CONTROLLER = ROOT / 'scratch/performance/run-pprint-canonical-constructor-original-body-20261008.py'
BASELINE_CONTROLLER_SHA = '8db91d3c713a9e6f72e98f82d81bc90edeb01c2374cac1a6e533930872da9b3b'
BASELINE_RECEIPT = DATA / 'pprint-canonical-constructor-baseline-original-body-20261008.json'
BASELINE_RECEIPT_SHA = 'fa207c5fe16cfa6aaeaf09bd90220184e039903872e79685ca41bbfde10539d7'
BASELINE_CONTROL = ROOT / 'build-repro/controls/sorted-key-ownership-r5-before-canonical-constructor-20261008/preserved-release-provenance.json'
BASELINE_CONTROL_SHA = '0f3df1c613842ff9ef3633ab36dfb99737966054b314f961df4d138c681ac33d'
CANDIDATE_PHASES = ['sorted7', 'iteration4', 'nested1', 'canonical10', 'fallback3', 'ownership2', 'owner2', 'namespace4', 'profile3', 'annotation2', 'cpp']

SIGNATURE = {'tuple_length': 3, 'text_length': 4200000, 'readable': True,
    'recursive': False, 'text_utf8_sha256': '15c269515f4a6ef7ae2566cccacd6b5c6613494a11c0d730e2e6c83c1010de6e'}


def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path): return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def file_map(directory):
    return {p.relative_to(directory).as_posix(): digest(p) for p in sorted(directory.rglob('*')) if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', required=True, choices=('candidate',))
    parser.add_argument('--source-inventory', required=True, type=Path)
    parser.add_argument('--source-inventory-sha256', required=True)
    parser.add_argument('--focused-receipt', required=True, type=Path)
    parser.add_argument('--focused-receipt-sha256', required=True)
    parser.add_argument('--control-manifest', type=Path)
    parser.add_argument('--control-manifest-sha256')
    parser.add_argument('--baseline-receipt', type=Path)
    parser.add_argument('--baseline-receipt-sha256')
    parser.add_argument('--prefix')
    args = parser.parse_args()
    assert sys.flags.optimize == 0 and sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CP.resolve()
    assert bool(args.control_manifest) == bool(args.control_manifest_sha256)
    assert bool(args.baseline_receipt) == bool(args.baseline_receipt_sha256)
    assert args.mode != 'candidate' or (args.control_manifest and args.baseline_receipt)
    assert args.mode != 'baseline' or args.baseline_receipt is None
    prefix = args.prefix or 'pprint-canonical-constructor-c3-original-body-20261008'
    assert re.fullmatch(r'[a-z0-9-]+', prefix) and not any(DATA.glob(prefix + '*'))
    output = DATA / (prefix + '.json')
    tracked = {}
    record = dict(status='preflight', terminal=False, mode=args.mode, diagnostic_only=True,
        full_validated=False, profile_enabled=False, repeat_per_runtime=1, timeout_seconds=300,
        baseline_kind=BASELINE_KIND, fixed_gate=None,
        scope='One unchanged original safe_repr body per runtime; unscored, no whole-suite/CPython-win claim',
        started_utc=datetime.now(timezone.utc).isoformat(), raw=[], idle_guards=[])

    def save(): output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))
    def track(path, expected=None):
        path = Path(path).resolve(strict=True)
        value = digest(path)
        assert expected is None or value == expected, str(path)
        assert str(path) not in tracked or tracked[str(path)] == value
        tracked[str(path)] = value
        return value
    def stable(): return all(Path(p).is_file() and digest(p) == value for p, value in tracked.items())
    def idle(label):
        raw = subprocess.check_output(['powershell', '-NoProfile', '-Command',
            'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'])
        rows = json.loads(raw.decode('utf-8-sig') or '[]')
        if isinstance(rows, dict): rows = [rows]
        tools = {'cl.exe', 'link.exe', 'ninja.exe', 'msbuild.exe', 'cmake.exe', 'ctest.exe', 'nmake.exe', 'lld-link.exe', 'clang-cl.exe'}
        busy = [r for r in rows if r['ProcessId'] != os.getpid() and
            (r['Name'].lower() in tools or r['Name'].lower().startswith(('python', 'xlang3')))]
        record['idle_guards'].append(dict(phase=label, allowed_controller_pid=os.getpid(), busy=busy))
        save()
        assert not busy, busy

    control = control_root = expected_tree = None
    try:
        save()
        idle('preflight')
        inventory_path = args.source_inventory.resolve(strict=True)
        validation_path = args.focused_receipt.resolve(strict=True)
        inventory, validation = read(inventory_path), read(validation_path)
        inventory_sha = track(inventory_path, args.source_inventory_sha256)
        validation_sha = track(validation_path, args.focused_receipt_sha256)
        sources = inventory['source_sha256']

        assert validation['terminal'] and validation['hashes_unchanged']
        assert validation['source_sha256'] == sources and validation['source_inventory_sha256'] == inventory_sha
        assert validation['status'] == 'targeted_correctness_passed'
        phases = validation['phases']
        assert len(phases) == len(CANDIDATE_PHASES) and {row['name'] for row in phases} == set(CANDIDATE_PHASES)
        assert all(row['exit_code'] == 0 and not row.get('timeout', False) and row.get('passed', True) for row in phases)
        for row in phases:
            if row['name'] != 'cpp': assert row['output_matches_expected'], row['name']
            for stream in ('stdout', 'stderr'):
                track(DATA / row[stream + '_log'], row[stream + '_sha256'])
            if row['name'] != 'cpp': assert (DATA / row['stderr_log']).read_bytes() == b''
        record['targeted_correctness'] = dict(receipt=str(validation_path), sha256=validation_sha,
            phases=list(CANDIDATE_PHASES), source_inventory_sha256=inventory_sha,
            scope='Exact targeted phase receipt only; fixed gate and full correctness have not been established by this diagnostic')
        for path, value in sources.items(): track(ROOT / path, value)
        release = file_map(EXE.parent)
        assert len(release) == 178
        for path, value in release.items(): track(EXE.parent / path, value)
        assert digest(EXE) == validation['candidate_binary_sha256']['exe']
        assert digest(EXE.with_name('xlang3_runtime.dll')) == validation['candidate_binary_sha256']['dll']
        assert validation['binaries_sha256'] == {(EXE.parent / path).relative_to(ROOT).as_posix(): value
            for path, value in release.items()}
        record.update(source_inventory=str(inventory_path), source_inventory_sha256=inventory_sha,
            validation=str(validation_path), validation_sha256=validation_sha, compiled_source_sha256=sources,
            release_sha256=release, release_file_count=len(release), gate_sha256=None,
            validation_scope='Same-candidate targeted correctness only; no gate/full-validity claim')
        if args.baseline_receipt:
            baseline_path = args.baseline_receipt.resolve(strict=True)
            assert baseline_path == BASELINE_RECEIPT.resolve(strict=True)
            assert args.baseline_receipt_sha256 == BASELINE_RECEIPT_SHA
            track(BASELINE_CONTROLLER, BASELINE_CONTROLLER_SHA)
            track(baseline_path, BASELINE_RECEIPT_SHA)
            baseline = read(baseline_path)
            assert baseline['terminal'] and baseline['mode'] == 'baseline' and baseline['hashes_unchanged']
            assert baseline['status'] == 'terminal_unscored_original_body_match'
            assert baseline['baseline_kind'] == BASELINE_KIND and baseline['controller_sha256'] == BASELINE_CONTROLLER_SHA
            assert baseline['diagnostic_only'] and not baseline['full_validated'] and baseline['gate_sha256'] is None
            assert baseline['targeted_correctness']['phases'] == ['sorted7', 'iteration4', 'nested1', 'cpp']
            assert baseline['release_file_count'] == 178 and len(baseline['raw']) == 2 and all(row['passed'] for row in baseline['raw'])
            assert baseline['child_sha256'] == CHILD_SHA and baseline['result_signature'] == SIGNATURE
            track(Path(baseline['source_inventory']), baseline['source_inventory_sha256'])
            track(Path(baseline['validation']), baseline['validation_sha256'])
            baseline_focus = read(Path(baseline['validation']))
            assert baseline_focus['status'] == 'targeted_correctness_passed' and baseline_focus['terminal'] and baseline_focus['hashes_unchanged']
            assert baseline_focus['source_sha256'] == baseline['compiled_source_sha256']
            assert baseline_focus['source_inventory_sha256'] == baseline['source_inventory_sha256']
            for row in baseline_focus['phases']:
                for stream in ('stdout', 'stderr'): track(DATA / row[stream + '_log'], row[stream + '_sha256'])
            for row in baseline['raw']:
                for stream in ('stdout', 'stderr'): track(DATA / row[stream + '_log'], row[stream + '_sha256'])
            record['baseline_receipt'] = dict(path=str(baseline_path), sha256=digest(baseline_path),
                controller=str(BASELINE_CONTROLLER), controller_sha256=BASELINE_CONTROLLER_SHA)
        if args.control_manifest:
            manifest_path = args.control_manifest.resolve(strict=True)
            assert manifest_path == BASELINE_CONTROL.resolve(strict=True)
            assert args.control_manifest_sha256 == BASELINE_CONTROL_SHA
            track(manifest_path, BASELINE_CONTROL_SHA)
            control, control_root = read(manifest_path), manifest_path.parent
            assert control['terminal'] and control['hashes_unchanged']
            assert len(control['files_sha256']) == control['file_count'] == 178
            assert control['source_snapshot_sha256'] == control['source_sha256']
            expected_sources = sources if args.mode == 'baseline' else baseline['compiled_source_sha256']
            expected_release = release if args.mode == 'baseline' else baseline['release_sha256']
            assert control['source_count'] == len(expected_sources)
            assert control['source_sha256'] == expected_sources and control['files_sha256'] == expected_release
            snapshot = control_root / control['source_snapshot_root']
            assert snapshot.resolve().is_relative_to(control_root.resolve())
            for path, value in control['files_sha256'].items(): track(control_root / path, value)
            for path, value in control['source_snapshot_sha256'].items(): track(snapshot / path, value)
            expected_tree = dict(control['files_sha256'])
            expected_tree.update({(Path(control['source_snapshot_root']) / p).as_posix(): value
                for p, value in control['source_snapshot_sha256'].items()})
            expected_tree[manifest_path.name] = digest(manifest_path)
            assert file_map(control_root) == expected_tree
            record['preserved_baseline_control'] = dict(path=str(manifest_path), sha256=digest(manifest_path),
                release_file_count=control['file_count'], source_count=control['source_count'],
                accepted_baseline=control.get('accepted_baseline'), full_validated=control.get('full_validated'),
                global_correctness_claim=control.get('global_correctness_claim'),
                known_new_correctness_limitation=control.get('known_new_correctness_limitation'),
                performance_acceptance=control.get('performance_acceptance'))
        record['control_scope'] = 'Complete preserved R5 control verified' if control else 'Complete current R5 bytes pinned; preserve these exact bytes before changing the engine'
        for path, value in ((CHILD, CHILD_SHA), (BENCHMARK, BENCHMARK_SHA),
            (CP.parent / 'Lib/pprint.py', PPRINT_SHA), (HOOK, HOOK_SHA),
            (CP, '4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9'),
            (CP.with_name('python314.dll'), '0f9857ffdfe010fe6b99328d58c2e3c7472ce75f336bf9c2ad9bd5bca3bce700')): track(path, value)
        record['controller_sha256'] = track(Path(__file__))
        cp_provenance = DATA / 'pyperformance-cpython3147-live-eval-full-fast-20261007-provenance.json'
        track(cp_provenance, '3d6c8cb2c72c786583c382b835e8e411f56ca285963bc2835f757a13f399f85c')
        cp_saved = read(cp_provenance)
        site = Path(cp_saved['dependency_site']).resolve(strict=True)
        metadata = {p.relative_to(site).as_posix(): track(p) for p in sorted(site.glob('*.dist-info/METADATA'))}
        assert metadata == {p.replace('\\', '/'): v for p, v in cp_saved['dependency_metadata_sha256'].items()}
        for package in (CP.parent / 'Lib/site-packages/pyperf', site / 'pyperf'):
            for path in sorted(package.rglob('*.py')): track(path)
        environment = os.environ.copy()
        for name in ('PYTHONOPTIMIZE', 'PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'): environment.pop(name, None)
        assert not environment.get('XLANG3_VM_OPCODE_TIMING')
        environment.update(XLANG3_PYTHON_LIB=str(CP.parent / 'Lib'), PYTHONPATH=os.pathsep.join((str(HOOK.parent), str(site))),
            PYTHONIOENCODING='utf-8', PYTHONUNBUFFERED='1')
        record.update(child_sha256=CHILD_SHA, result_signature=SIGNATURE, hashes_before=dict(tracked),
            child_environment={name: environment[name] for name in ('XLANG3_PYTHON_LIB', 'PYTHONPATH', 'PYTHONIOENCODING', 'PYTHONUNBUFFERED')})
        for role, executable in (('cpython3147', CP), ('candidate-xlang3', EXE)):
            idle('before-' + role)
            assert stable() and file_map(EXE.parent) == release
            logs = [DATA / (prefix + '-' + role + suffix) for suffix in ('.stdout.log', '.stderr.log')]
            row = dict(runtime=role, command=[str(executable), str(CHILD), '--benchmark-script', str(BENCHMARK)],
                stdout_log=logs[0].name, stderr_log=logs[1].name, timeout=False, passed=False)
            record['raw'].append(row)
            save()
            env = dict(environment, PYTHONPYCACHEPREFIX=str(ROOT / 'scratch/performance' / ('pycache-' + prefix) / role))
            child = None
            try:
                with logs[0].open('xb') as stdout, logs[1].open('xb') as stderr:
                    child = subprocess.Popen(row['command'], cwd=ROOT, env=env, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr)
                    row['pid'] = child.pid
                    try: row['exit_code'] = child.wait(timeout=300)
                    except subprocess.TimeoutExpired:
                        row['timeout'] = True
                        subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=15)
                        if child.poll() is None: child.kill()
                        row['exit_code'] = child.wait(timeout=15)
            except Exception as error: row['error'] = type(error).__name__ + ': ' + str(error)
            finally:
                if child is not None and child.poll() is None:
                    child.kill()
                    child.wait(timeout=15)
                for stream, path in zip(('stdout', 'stderr'), logs): row[stream + '_sha256'] = digest(path) if path.is_file() else None
                try:
                    events = [json.loads(line) for line in logs[0].read_text(encoding='utf-8-sig').splitlines() if line.strip()]
                    row['events'] = events
                    event = events[-1]
                    row['passed'] = len(events) == 2 and events[0]['status'] == 'body_start' and event['status'] == 'body_complete' and \
                        row.get('exit_code') == 0 and not row['timeout'] and logs[1].read_bytes() == b'' and event['success'] and event['hashes_unchanged'] and \
                        event['runtime'] == ('cpython' if role == 'cpython3147' else 'xlang3') and event['version_info'] == [3, 14, 7] and \
                        Path(event['executable']).resolve() == executable.resolve() and event['optimization_level'] == 0 and not event['profile_enabled'] and \
                        event['benchmark_source_sha256'] == BENCHMARK_SHA and event['pprint_source_sha256'] == PPRINT_SHA and \
                        event['input_length'] == 100000 and event['input_alias_preserved'] and event['repeat'] == 1 and all(event['prechecks'].values()) and event['result_signature'] == SIGNATURE
                    row['elapsed_seconds_diagnostic_only'] = event.get('elapsed_seconds_diagnostic_only')
                except Exception as error: row['parse_or_validation_error'] = type(error).__name__ + ': ' + str(error)
                save()
            assert stable() and file_map(EXE.parent) == release
        idle('after-children')
        record['status'] = 'terminal_unscored_original_body_match' if all(r['passed'] for r in record['raw']) else 'terminal_unscored_original_body_failure_or_mismatch'
    except BaseException as error: record.update(status='terminal_failed_diagnostic_controller', error=type(error).__name__ + ': ' + str(error))
    finally:
        after = {p: digest(p) if Path(p).is_file() else None for p in tracked}
        tree_stable = 'release' in locals() and file_map(EXE.parent) == release
        if control is not None: tree_stable = tree_stable and expected_tree is not None and file_map(control_root) == expected_tree
        record.update(terminal=True, completed_utc=datetime.now(timezone.utc).isoformat(), hashes_after=after,
            hashes_unchanged=after == tracked and tree_stable)
        if not record['hashes_unchanged']: record['status'] = 'terminal_failed_hash_integrity'
        save()
    print('Original pprint diagnostic terminal:', record['status'], flush=True)
    return 0 if record['status'] == 'terminal_unscored_original_body_match' else 1


if __name__ == '__main__': raise SystemExit(main())
