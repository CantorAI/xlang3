"""Same-combined-candidate default11 gate only; no correctness or benchmark rerun."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path('D:/CantorAI/xlang3')
CP = Path('C:/Python/Python314/python.exe')
BUILD_DIR = ROOT / 'build-repro/main-verify-20261006'
RELEASE = BUILD_DIR / 'Release'
BASELINE = ROOT / 'build-repro/Release'
DATA = ROOT / 'doc/performance/data'
R7 = ROOT / 'scratch/performance/validate-two-argument-double-ir-plan-r2-trial-r7-gate-telemetry-proposed-20261010.py'
R7_SHA = '34f9e66330bbf8f3a674c8fc4448c494f3f9b3da4e84eca25b356c5bf0b3231e'
WATCH = ROOT / 'scratch/performance/gc-phase-dormant-msbuild-activity-watch-20261009.py'
WATCH_SHA = '6dea8abf4f66ff0d2a95830e4c560202359c57502f0f635ecf9e1dda5494bd48'
GATE = ROOT / 'benchmarks/check_regression.py'
GATE_SHA = '579934598caddc37409fed03181b0cd42408853a34bde65dff48b777e4e8ffef'
CORRECTNESS_CONTROLLER = ROOT / 'scratch/performance/check-frame-f-code-cache-r2-correctness-proposed-20261010.py'
CORRECTNESS_SHA = '9c3cd1f0b60f1b8507df04d1789e3a8ddae722724c65a30a1dbc7ff856c123ff'
INTEGRATOR = ROOT / 'scratch/performance/apply-build-frame-f-code-cache-r2-proposed-20261010.py'
INTEGRATOR_SHA = '6964467891efcc7290702e04fd529f164834b247404231921216ec1882a0c31f'
HEAD = '46496cf5ddb903e25927cce616c29c2f81032566'

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def require(value, message):
    if not value:
        raise RuntimeError(message)

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application', type=Path, required=True)
    parser.add_argument('--application-sha256', required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--build-sha256', required=True)
    parser.add_argument('--semantic-receipt', type=Path, required=True)
    parser.add_argument('--semantic-sha256', required=True)
    parser.add_argument('--correctness-receipt', type=Path, required=True)
    parser.add_argument('--correctness-sha256', required=True)
    parser.add_argument('--known-worker-proof', type=Path)
    parser.add_argument('--known-worker-sha256')
    parser.add_argument('--prefix', default='frame-f-code-cache-r2-fixed-gate-20261010')
    args = parser.parse_args()
    require(sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated and not sys.flags.optimize,
            'Exact isolated unoptimized CPython3.14.7 required')
    require(Path(sys.executable).resolve() == CP.resolve(), 'Wrong controller executable')
    require(re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9_.-]+', args.prefix), 'Unsafe prefix')
    require(bool(args.known_worker_proof) == bool(args.known_worker_sha256), 'Worker proof/path pin must be supplied together')
    sys.dont_write_bytecode = True
    require(sha(R7) == R7_SHA, 'Reviewed timing implementation changed')
    base = load('frame_cache_gate_telemetry', R7)
    Activity, tree, document, normalized = base.Activity, base.tree, base.document, base.normalized
    output = DATA / (args.prefix + '.json')
    gate = DATA / (args.prefix + '-fixed-gate.json')
    require(not output.exists() and not gate.exists(), 'Never overwrite prior evidence')
    pins = {}
    release = baseline = None
    record = dict(terminal=False, passed=False, mode='gate', status='preflight',
        controller_sha256=sha(__file__), phases=[], correctness_passed=False,
        fixed_gate_passed=False, full_validated=False, official_benchmark_completed=False,
        numeric_parent_accepted=False, dormant_worker_exception_requested=bool(args.known_worker_proof),
        dormant_worker_exception_used=False, scope='Fresh unchanged default11 gate after authenticated combined source148 correctness. No correctness rerun, old numeric acceptance or official benchmark claim.')

    def save():
        output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')

    def pin(path, expected=None):
        path = Path(path).resolve(strict=True)
        digest = sha(path)
        require(expected is None or digest == expected, 'Input hash mismatch: ' + str(path))
        require(str(path) not in pins or pins[str(path)] == digest, 'Conflicting pin')
        pins[str(path)] = digest
        return path

    def test_names():
        return sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / 'tests').rglob('*')
            if p.is_file() and p.suffix in base.TEST_SUFFIXES and '__pycache__' not in p.parts)

    def stable():
        require(all(sha(p) == h for p, h in pins.items()), 'Input bytes changed')
        require(tree(RELEASE) == release and tree(BASELINE) == baseline, 'Release/baseline set or bytes changed')
        require(test_names() == record['test_input_names'], 'Test input set changed')
        require(subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT).decode().strip() == HEAD, 'HEAD changed')

    def phase(name, command, cap, expected=None, timed=False):
        stable()
        row = dict(name=name, command=list(map(str, command)), timeout_seconds=cap,
                   timed=timed, passed=False, timing_accepted=False, started_unix=time.time())
        record['phases'].append(row)
        stdout = DATA / (args.prefix + '-' + name + '.stdout.log')
        stderr = DATA / (args.prefix + '-' + name + '.stderr.log')
        require(not stdout.exists() and not stderr.exists(), 'Raw log already exists')
        child = watcher = None
        cleanup_errors = []
        env = os.environ.copy()
        for k in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE', 'PYTHONHOME', 'XLANG3_VM_OPCODE_TIMING'):
            env.pop(k, None)
        env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
        if timed:
            env['PYTHONPYCACHEPREFIX'] = str(ROOT / 'scratch/performance' / (args.prefix + '-pycache') / name)
        try:
            with stdout.open('xb') as out, stderr.open('xb') as err:
                watcher = Activity(helper, args.prefix, name, timed)
                require(watcher.observe(manager_only=True), 'Preflight foreign process/scanner refusal')
                child = subprocess.Popen(row['command'], cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                                         stdout=out, stderr=err,
                                         creationflags=subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.CREATE_NO_WINDOW)
                row['pid'] = child.pid
                require(watcher.observe(), 'Post-launch overlap/scanner failure')
                watcher.launch_watch()
                row['exit_code'] = child.wait(timeout=cap)
        except BaseException as error:
            row['error'] = repr(error)
        finally:
            if child is not None and child.poll() is None:
                try:
                    subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)],
                                   capture_output=True, timeout=15, check=True)
                    child.wait(timeout=15)
                except BaseException as error:
                    cleanup_errors.append(repr(error))
                    try:
                        child.kill()
                        child.wait(timeout=15)
                    except BaseException as fallback:
                        cleanup_errors.append(repr(fallback))
            row.update(stdout=stdout.name, stderr=stderr.name,
                       stdout_sha256=sha(stdout) if stdout.exists() else None,
                       stderr_sha256=sha(stderr) if stderr.exists() else None,
                       owned_child_cleanup_completed=child is not None and child.poll() is not None,
                       cleanup_errors=cleanup_errors, finished_unix=time.time())
            if watcher is not None:
                try:
                    row['external_process_watch'] = watcher.finish()
                except BaseException as error:
                    row['watcher_finish_error'] = repr(error)
            try:
                stable()
                row['hashes_unchanged'] = True
            except BaseException as error:
                row['hashes_unchanged'] = False
                row['post_hash_error'] = repr(error)
            row['passed'] = (row.get('exit_code') == 0 and not row.get('error')
                and row['owned_child_cleanup_completed'] and not cleanup_errors
                and row.get('external_process_watch', {}).get('observation_valid', False)
                and row['hashes_unchanged'] and not row.get('watcher_finish_error'))
            if expected is not None:
                row['output_matches_expected'] = stdout.exists() and normalized(stdout.read_bytes()) == expected
                row['passed'] = row['passed'] and row['output_matches_expected'] and stderr.read_bytes() == b''
            row['timing_accepted'] = timed and row['passed']
            save()
        require(row['passed'], 'Phase failed or invalid: ' + name)
        return stdout.read_bytes()

    try:
        pin(__file__)
        pin(R7, R7_SHA)
        pin(INTEGRATOR, INTEGRATOR_SHA)
        pin(CORRECTNESS_CONTROLLER, CORRECTNESS_SHA)
        app = document(pin(args.application, args.application_sha256))
        build = document(pin(args.build, args.build_sha256))
        semantic = document(pin(args.semantic_receipt, args.semantic_sha256))
        prior = document(pin(args.correctness_receipt, args.correctness_sha256))
        require(app['terminal'] and app['passed'] and app['status'] == 'applied' and app['head'] == HEAD
                and app['controller_sha256'] == INTEGRATOR_SHA and app['numeric_parent_accepted'] is False, 'Wrong combined application')
        require(build['terminal'] and build['passed'] and build['exit_code'] == 0
                and build['status'] == 'build_passed' and build['sources_unchanged']
                and build['owned_child_cleanup_completed'] and build['controller_sha256'] == INTEGRATOR_SHA
                and build['application_sha256'] == args.application_sha256
                and Path(build['application_path']).resolve() == args.application.resolve(), 'Build/app mismatch')
        sources, release, baseline = app['source_sha256'], build['release_sha256'], app['fixed_baseline_sha256']
        require(app['source_count'] == len(sources) == 148 and build['source_sha256'] == sources
                and len(release) == 178 and len(baseline) == 177, 'Combined inventories mismatch')
        require(prior['terminal'] and prior['passed'] and prior['correctness_passed'] and prior['hashes_unchanged']
                and prior['status'] == 'correctness_passed_performance_pending'
                and prior['controller_sha256'] == CORRECTNESS_SHA and prior['inherited_correctness'] is False,
                'Fresh combined correctness not passed')
        require(prior['application_sha256'] == args.application_sha256 and prior['build_sha256'] == args.build_sha256
                and prior['combined_semantic_receipt_sha256'] == args.semantic_sha256
                and prior['source_sha256'] == sources and prior['release_sha256'] == release
                and prior['fixed_baseline_sha256'] == baseline, 'Correctness candidate differs')
        require(prior['protected_test_input_sha256'] == app['protected_test_input_sha256']
                and prior['unowned_tracked_dirty_sha256'] == app['unowned_tracked_dirty_sha256']
                and prior['fixture_inventory'] == dict(core=408, compat=11, expected_failures=3), 'Protected maps/inventory mismatch')
        require(prior['hashes_before'] == prior['hashes_after'], 'Prior input drift')
        for path, digest in prior['hashes_before'].items():
            pin(path, digest)  # Historical BEFORE source/binary pins already resolve to verified archive copies.
        require(semantic['terminal'] and semantic['passed'] and semantic['inputs_unchanged']
                and semantic['application_sha256'] == args.application_sha256
                and semantic['build_sha256'] == args.build_sha256
                and semantic['results'] == prior['authenticated_focused_results'], 'Combined focused receipt differs')
        ctest = [base.CTEST, '--test-dir', BUILD_DIR, '-C', 'Release']
        commands = [('python-fixtures', [CP, '-I', ROOT / 'tests/run_fixtures.py', RELEASE / 'xlang3.exe'], 1200),
                    ('ctest-inventory', [*ctest, '--show-only=json-v1'], 120),
                    ('ctest-all55', [*ctest, '--output-on-failure', '--verbose', '--parallel', '1'], 1800)]
        require([r['name'] for r in prior['phases']] == [c[0] for c in commands], 'Fresh prior phase set changed')
        for row, (name, command, cap) in zip(prior['phases'], commands):
            require(row['command'] == list(map(str, command)) and row['timeout_seconds'] == cap
                    and row['passed'] and row['exit_code'] == 0 and not row['timed']
                    and not row['timing_accepted'] and row['owned_child_cleanup_completed']
                    and not row['cleanup_errors'] and row['hashes_unchanged']
                    and row['external_process_watch']['observation_valid']
                    and not row['external_process_watch']['scanner_errors'], 'Prior fresh correctness phase invalid')
            pin(DATA / row['stdout'], row['stdout_sha256'])
            err = pin(DATA / row['stderr'], row['stderr_sha256'])
            require(err.read_bytes() == b'', 'Prior correctness stderr not empty')
            pin(DATA / row['external_process_watch']['log'], row['external_process_watch']['sha256'])
        require((DATA / prior['phases'][0]['stdout']).read_bytes() == b'', 'Full Python output differs')
        inventory = document(DATA / prior['phases'][1]['stdout'])
        require(inventory == prior['ctest_inventory'], 'CTest inventory raw mismatch')
        names = [t['name'] for t in inventory['tests']]
        text = normalized((DATA / prior['phases'][2]['stdout']).read_bytes())
        passed = re.findall(r'Test\s+#\d+:\s+(\S+)\s+\.+\s+Passed', text)
        require(len(names) == len(set(names)) == len(passed) == 55 and set(passed) == set(names)
                and passed == prior['ctest_actual_passed_names']
                and '100% tests passed, 0 tests failed out of 55' in text, 'All55 actual results mismatch')
        require(prior['api_checks']['passed'] and prior['api_checks']['cases'] == ['old_xlang_sqlite_api', 'python_sqlite3_api']
                and prior['api_checks']['covered_by'] == 'ctest-all55/xlang3_cli_sqlite_module_imports'
                and all('sqlite fixture ' + name + ' ok' in text for name in prior['api_checks']['cases']), 'API2 actual results missing')
        require([r['name'] for r in prior['strict_diagnostics']] == ['probe-canonical', 'probe-assigned_metadata'], 'Strict diagnostics missing')
        for row in prior['strict_diagnostics']:
            pin(DATA / row['stdout'], row['stdout_sha256'])
            pin(DATA / row['stderr'], row['stderr_sha256'])
            pin(DATA / row['external_process_watch']['log'], row['external_process_watch']['sha256'])
        archive = document(Path(app['archive_path']) / 'provenance.json')
        require(archive['accepted'] is False and archive['source_count'] == 146
                and archive['source_sha256'] == app['before_source_sha256'], 'Unaccepted parent archive mismatch')
        require(tree(Path(app['archive_path']) / 'Release') == app['before_release_sha256'], 'Archived Release changed')
        pin(GATE, GATE_SHA)
        for case in base.CASES:
            pin(ROOT / 'benchmarks/cases' / (case + '.py'))
        pin(WATCH, WATCH_SHA)
        helper = load('frame_cache_gate_scan', WATCH)
        require(helper._admission is None, 'Admission baseline already initialized')
        if args.known_worker_proof:
            base.bind_worker_proof(args.known_worker_proof, args.known_worker_sha256, helper, pin)
        record.update(correctness_passed=True, correctness_receipt_sha256=args.correctness_sha256,
            application_sha256=args.application_sha256, build_sha256=args.build_sha256,
            combined_semantic_receipt_sha256=args.semantic_sha256, source_sha256=sources, source_count=148,
            release_sha256=release, fixed_baseline_sha256=baseline,
            authenticated_fresh_correctness_phases=prior['phases'], retained_strict_diagnostics=prior['strict_diagnostics'],
            dormant_worker_admission=helper._admission, test_input_names=prior['test_input_names'], hashes_before=dict(pins))
        stable()
        save()
        phase('fixed-gate', [CP, '-I', GATE, '--baseline', BASELINE / 'xlang3.exe',
              '--candidate', RELEASE / 'xlang3.exe', '--output', gate,
              '--repeats', '21', '--warmup', '5', '--threshold', '0.10'], 1800, timed=True)
        report = document(gate)
        require(report['status'] == 'pass' and set(report['cases']) == set(base.CASES)
                and report['repeats'] == 21 and report['warmup'] == 5 and report['threshold'] == .10, 'Default11 gate not passed')
        require(all(v['status'] == 'pass' and all(len(a['baseline_seconds']) == len(a['candidate_seconds']) == 21
                    for a in v['attempts']) for v in report['cases'].values()), 'Gate arrays incomplete')
        record.update(fixed_gate_passed=True, passed=True, status='correctness_and_fixed_gate_passed_no_original_benchmark')
        stable()
    except BaseException as error:
        record.update(passed=False, status='failed_or_invalid_gate', error=repr(error))
    finally:
        record['terminal'] = True
        record['hashes_after'] = {p: sha(p) if Path(p).is_file() else None for p in pins}
        record['hashes_unchanged'] = all(record['hashes_after'][p] == h for p, h in pins.items())
        if not record['hashes_unchanged']:
            record.update(passed=False, fixed_gate_passed=False, status='terminal_invalid_input_drift')
        if gate.exists():
            record.update(fixed_gate_path=gate.name, fixed_gate_sha256=sha(gate))
        for row in record['phases']:
            watch = row.get('external_process_watch')
            if watch and (DATA / watch['log']).exists():
                try:
                    observations = [json.loads(line) for line in (DATA / watch['log']).read_bytes().splitlines()]
                    record['dormant_worker_exception_used'] |= any(r.get('dormant_worker_rows') for r in observations)
                except BaseException as error:
                    record.update(passed=False, fixed_gate_passed=False, status='invalid_usage_telemetry',
                                  usage_telemetry_error=repr(error))
        record['finished_unix'] = time.time()
        save()
    print(record['status'], sha(output), flush=True)
    return 0 if record['passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
