"""Fresh combined cache/numeric correctness; strict probes separate, no timings."""
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
BASE = ROOT / 'scratch/performance/validate-two-argument-double-ir-plan-r2-trial-r4-proposed-20261010.py'
BASE_SHA = 'cb4ab8ceba90b041b50d75326fc9af31e4a029672ec6a723b17d1c4cc5652de0'
INTEGRATOR = ROOT / 'scratch/performance/apply-build-frame-f-code-cache-r2-proposed-20261010.py'
INTEGRATOR_SHA = '6964467891efcc7290702e04fd529f164834b247404231921216ec1882a0c31f'
WATCH = ROOT / 'scratch/performance/gc-phase-dormant-msbuild-activity-watch-20261009.py'
WATCH_SHA = '6dea8abf4f66ff0d2a95830e4c560202359c57502f0f635ecf9e1dda5494bd48'
REFERENCE = DATA / 'frame-f-code-r2-reference-20261010.json'
REFERENCE_SHA = '04a8c326a4393ad9ab7ee2dfc3c5d97e45a1c32d643aa378347cca5abacdb029'
PROBE = ROOT / 'scratch/performance/frame-f-code-preexisting-identity-metadata-probe-proposed-20261010.py'
HEAD = '46496cf5ddb903e25927cce616c29c2f81032566'
TEST_SUFFIXES = {'.py', '.ps1', '.out', '.cpp', '.h', '.c', '.json', '.toml', '.txt', '.x', '.xlang'}

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def require(condition, message):
    if not condition:
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
    parser.add_argument('--prefix', default='frame-f-code-cache-r2-correctness-20261010')
    args = parser.parse_args()
    require(sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated and not sys.flags.optimize,
            'Exact isolated unoptimized CPython3.14.7 required')
    require(Path(sys.executable).resolve() == CP.resolve(), 'Wrong controller executable')
    require(re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9_.-]+', args.prefix), 'Unsafe prefix')
    sys.dont_write_bytecode = True
    require(sha(BASE) == BASE_SHA, 'Borrowed correctness implementation changed')
    base = load('frame_cache_correctness_base', BASE)
    Activity, normalized, tree, document = base.Activity, base.normalized, base.tree, base.document
    output = DATA / (args.prefix + '.json')
    require(not output.exists(), 'Never overwrite evidence')
    pins = {}
    release = baseline = None
    record = dict(terminal=False, passed=False, status='preflight', scored=False,
        controller_sha256=sha(__file__), phases=[], strict_diagnostics=[],
        correctness_passed=False, inherited_correctness=False, numeric_parent_accepted=False,
        fixed_gate_passed=False, performance_accepted=False, full_validated=False,
        scope='Fresh combined registered correctness only. Canonical identity and assigned-code metadata are separate strict diagnostics, not repaired or relaxed by this cache.')

    def save():
        output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')

    def pin(path, expected=None):
        path = Path(path).resolve(strict=True)
        value = sha(path)
        require(expected is None or value == expected, 'Input hash mismatch: ' + str(path))
        require(str(path) not in pins or pins[str(path)] == value, 'Conflicting pin')
        pins[str(path)] = value
        return path

    def verify(directory, mapping):
        for name, digest in mapping.items():
            target = directory / name
            require(target.resolve().is_relative_to(directory.resolve()), 'Unsafe mapped path')
            pin(target, digest)

    def test_names():
        return sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / 'tests').rglob('*')
                      if p.is_file() and p.suffix in TEST_SUFFIXES and '__pycache__' not in p.parts)

    def stable():
        require(all(sha(p) == h for p, h in pins.items()), 'Input bytes changed')
        require(tree(RELEASE) == release and tree(BASELINE) == baseline, 'Release/baseline set or bytes changed')
        require(subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT).decode().strip() == HEAD, 'HEAD changed')
        require(test_names() == record['test_input_names'], 'Test input set changed')

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

    def strict_probe(case, expected):
        try:
            phase('probe-' + case, [RELEASE / 'xlang3.exe', PROBE, case], 120, expected)
        except RuntimeError:
            row = record['phases'][-1]
            require(row['name'] == 'probe-' + case and row.get('exit_code') in (0, 1)
                and not row.get('error') and row.get('owned_child_cleanup_completed')
                and not row.get('cleanup_errors') and row.get('hashes_unchanged')
                and row.get('external_process_watch', {}).get('observation_valid')
                and not row.get('watcher_finish_error'), 'Probe execution invalid')
        row = record['phases'].pop()
        row.update(strict_expected_match=row['passed'], included_in_registered_correctness=False,
            baseline_strict_failure_preserved=True, status='strict_pass' if row['passed'] else 'strict_fail')
        record['strict_diagnostics'].append(row)
        save()

    try:
        pin(__file__)
        pin(BASE, BASE_SHA)
        pin(INTEGRATOR, INTEGRATOR_SHA)
        app = document(pin(args.application, args.application_sha256))
        build = document(pin(args.build, args.build_sha256))
        require(app['terminal'] and app['passed'] and app['status'] == 'applied' and app['head'] == HEAD, 'Application failed')
        require(app['controller_sha256'] == build['controller_sha256'] == INTEGRATOR_SHA, 'Wrong producers')
        require(build['terminal'] and build['passed'] and build['exit_code'] == 0 and build['status'] == 'build_passed'
                and build['sources_unchanged'] and build['owned_child_cleanup_completed'], 'Build not passed/clean')
        require(build['application_sha256'] == args.application_sha256
                and Path(build['application_path']).resolve() == args.application.resolve(), 'Build/app mismatch')
        sources = app['source_sha256']
        release, baseline = build['release_sha256'], app['fixed_baseline_sha256']
        require(app['source_count'] == len(sources) == 148 and build['source_sha256'] == sources
                and len(release) == 178 and len(baseline) == 177, 'Current inventories mismatch')
        require(app['numeric_parent_accepted'] is False and app['inherited_correctness'] is False, 'Unaccepted lineage lost')
        require(len(app['incremental_owned_targets']) == 8 and len(app['cumulative_owned_targets']) == 13
                and len(app['cumulative_owned_engine_paths']) == 7, 'Ownership counts mismatch')
        proposal = document(pin(app['proposal_path'], app['proposal_sha256']))
        require(sources == proposal['candidate_recorded_source_sha256']
                and app['before_source_sha256'] == proposal['parent_recorded_source_sha256'], 'Proposal map mismatch')
        require(set(app['incremental_owned_targets']) == set(proposal['candidate_source_sha256']), 'Incremental scope mismatch')
        pin(proposal['patch'], proposal['patch_sha256'])
        verify(ROOT, sources)
        verify(ROOT, app['unowned_tracked_dirty_sha256'])
        verify(ROOT, app['protected_test_input_sha256'])
        verify(RELEASE, release)
        verify(BASELINE, baseline)
        control_path = pin(app['accepted_control_manifest_path'], app['accepted_control_manifest_sha256'])
        control = document(control_path)
        require(control['full_validated'] and control['fixed_gate_passed'] and baseline == control['fixed_baseline_sha256'], 'Accepted control/baseline mismatch')
        verify(control_path.parent / 'sources', control['source_sha256'])
        require(tree(control_path.parent / 'Release') == control['release_sha256'], 'Accepted control Release changed')
        archive_path = Path(app['archive_path'])
        archive = document(pin(archive_path / 'provenance.json', app['archive_manifest_sha256']))
        require(archive['terminal'] and archive['accepted'] is False and archive['source_count'] == 146
                and archive['source_sha256'] == app['before_source_sha256']
                and archive['original_recorded_source_sha256'] == app['original_recorded_source_sha256']
                and len(archive['original_recorded_source_sha256']) == 145, 'Unaccepted archive mismatch')
        verify(archive_path / 'sources', archive['source_sha256'])
        verify(archive_path / 'unowned', archive['unowned_tracked_dirty_sha256'])
        require(tree(archive_path / 'Release') == app['before_release_sha256'] == archive['release_sha256'], 'Archived numeric Release changed')
        pin(app['previous_application_path'], app['previous_application_sha256'])
        pin(app['previous_build_path'], app['previous_build_sha256'])
        pin(DATA / build['log'], build['log_sha256'])
        require(build['command'][:3] == ['cmd.exe', '/d', '/c'] and len(build['command']) == 4, 'Wrong build command')
        pin(build['command'][3], build['build_command_source_sha256'])
        pin(CP, '4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9')
        pin(CP.parent / 'python314.dll', '0f9857ffdfe010fe6b99328d58c2e3c7472ce75f336bf9c2ad9bd5bca3bce700')
        reference = document(pin(REFERENCE, REFERENCE_SHA))
        require(reference['terminal'] and reference['reference_passed'] and reference['inputs_unchanged'], 'CP-first reference invalid')
        for row in reference['results']:
            pin(DATA / row['stdout'], row['stdout_sha256'])
            pin(DATA / row['stderr'], row['stderr_sha256'])
        # BEFORE source145/current Release pins resolve to preserved parent bytes,
        # never to the now changed candidate. Other immutable reference pins stay direct.
        historical_resolution = {}
        for path, digest in reference['pins'].items():
            original = Path(path).resolve()
            resolved = original
            if original.is_relative_to(RELEASE):
                resolved = archive_path / 'Release' / original.relative_to(RELEASE)
            elif original.is_relative_to(ROOT):
                name = original.relative_to(ROOT).as_posix()
                if name in archive['original_recorded_source_sha256']:
                    resolved = archive_path / 'sources' / name
            historical_resolution[str(original)] = str(pin(resolved, digest))
        for case in ('fixture', 'canonical', 'assigned_metadata'):
            cp_row = next(r for r in reference['results'] if r['runtime'] == 'cpython3147' and r['case'] == case)
            require(cp_row['strict_expected_match'] and cp_row['exit_code'] == 0
                    and (DATA / cp_row['stderr']).read_bytes() == b'', 'CP strict transcript failed')
        pin(PROBE, '6c27c8f42b72839186a5e0a6d1c31acda07e4486561008837756b69f117b74ea')
        fixtures = [('frame_f_code_cache', 'c59eb4b0869352fe2e2d9a140215d23a43432f997176d4a19cd08a97f104be17',
                     '2e3f8ca90f1a219f82b7405ce1931a145534440a81fb09f331d4d01d6c2e0917'),
                    ('two_argument_double_ir_plan', '406434854572605d6e699d54bcd2c1b9b524b1f403aeec70f7190c5f57b514f4',
                     '154e54becd1e12cb2d4a8461bc8739f786a9a9136a1cbec8be8bf40ad6adaeac')]
        for name, source_sha, expected_sha in fixtures:
            pin(ROOT / 'tests/fixtures/core' / (name + '.py'), source_sha)
            pin(ROOT / 'tests/fixtures/expected' / (name + '.out'), expected_sha)
        semantic = document(pin(args.semantic_receipt, args.semantic_sha256))
        semantic_producer = ROOT / 'scratch/performance/check-frame-f-code-cache-candidate-20261010.py'
        pin(semantic_producer, '9ded1d5a0e0b24c3286b95c092aeca2f9fafdcdc11b89727b13150c1140290e8')
        require(semantic['terminal'] and semantic['passed'] and semantic['inputs_unchanged']
                and semantic['scored'] is False and semantic['controller_sha256'] == sha(semantic_producer)
                and semantic['application_sha256'] == args.application_sha256
                and semantic['build_sha256'] == args.build_sha256, 'Combined candidate focus mismatch')
        require([r['case'] for r in semantic['results']] == [f[0] for f in fixtures], 'Focus ordering mismatch')
        for path, digest in semantic['pins'].items():
            pin(path, digest)
        for row, (name, _, expected_sha) in zip(semantic['results'], fixtures):
            require(row['passed'] and row['exit_code'] == 0 and row['direct_child_waited']
                    and row['expected_sha256'] == expected_sha
                    and list(map(Path, row['command'])) == [RELEASE / 'xlang3.exe', ROOT / 'tests/fixtures/core' / (name + '.py')],
                    'Combined focus row invalid')
            stdout = pin(DATA / row['stdout'], row['stdout_sha256'])
            stderr = pin(DATA / row['stderr'], row['stderr_sha256'])
            require(normalized(stdout.read_bytes()) == normalized((ROOT / 'tests/fixtures/expected' / (name + '.out')).read_bytes())
                    and stderr.read_bytes() == b'', 'Combined focus raw output mismatch')
        CTEST = pin(base.CTEST)
        pin(BUILD_DIR / 'CTestTestfile.cmake')
        pin(WATCH, WATCH_SHA)
        helper = load('frame_cache_untimed_scan', WATCH)
        require(helper._admission is None, 'No dormant admission in correctness')
        record['test_input_names'] = test_names()
        for name in record['test_input_names']:
            pin(ROOT / name)
        runner = (ROOT / 'tests/run_fixtures.py').read_text(encoding='utf-8')
        core = re.search(r'CORE_CASES\s*=\s*"""(.*?)"""\.split\(\)', runner, re.S).group(1).split()
        compat = re.search(r'SECTION_CASES\s*=\s*"""(.*?)"""\.split\(\)', runner, re.S).group(1).split()
        require(len(core) == len(set(core)) == 408 and len(compat) == len(set(compat)) == 11
                and runner.count('    assert_failure(executable,') == 3
                and all(core.count(name) == 1 for name, _, _ in fixtures), 'Registered inventory changed')
        record.update(application_sha256=args.application_sha256, build_sha256=args.build_sha256,
            source_sha256=sources, source_count=148, release_sha256=release, fixed_baseline_sha256=baseline,
            protected_test_input_sha256=app['protected_test_input_sha256'],
            unowned_tracked_dirty_sha256=app['unowned_tracked_dirty_sha256'],
            cumulative_owned_targets=app['cumulative_owned_targets'],
            fixture_inventory=dict(core=408, compat=11, expected_failures=3),
            historical_reference_sha256=REFERENCE_SHA, historical_reference_pin_resolution=historical_resolution,
            combined_semantic_receipt_sha256=args.semantic_sha256,
            authenticated_focused_results=semantic['results'],
            hashes_before=dict(pins))
        stable()
        save()
        # These two focused rows are actual POST-combined-candidate executions.
        # Full registered correctness below is still fresh; no numeric-parent reuse.
        strict_probe('canonical', 'frame uses exact canonical function code object')
        strict_probe('assigned_metadata', 'frame preserves exact assigned code object and metadata')
        phase('python-fixtures', [CP, '-I', ROOT / 'tests/run_fixtures.py', RELEASE / 'xlang3.exe'], 1200, '')
        ctest = [CTEST, '--test-dir', BUILD_DIR, '-C', 'Release']
        inventory = json.loads(phase('ctest-inventory', [*ctest, '--show-only=json-v1'], 120))
        names = [t['name'] for t in inventory['tests']]
        require(len(names) == len(set(names)) == 55, 'Expected all55 CTest inventory')
        for test in inventory['tests']:
            require(not any('/Debug/' in str(p).replace('\\', '/') for p in test['command']), 'Debug CTest command')
            for part in test['command']:
                if Path(str(part)).name.lower().startswith('python') and str(part).lower().endswith('.exe'):
                    require(Path(part).resolve() == CP.resolve(), 'CTest uses another CPython')
        record['ctest_inventory'] = inventory
        raw = phase('ctest-all55', [*ctest, '--output-on-failure', '--verbose', '--parallel', '1'], 1800)
        text = normalized(raw)
        passed = re.findall(r'Test\s+#\d+:\s+(\S+)\s+\.+\s+Passed', text)
        require(len(passed) == 55 and set(passed) == set(names)
                and '100% tests passed, 0 tests failed out of 55' in text, 'CTest actual results mismatch')
        record['ctest_actual_passed_names'] = passed
        native = ROOT / 'tests/native/sqlite'
        sqlite = next((t for t in inventory['tests'] if t['name'] == 'xlang3_cli_sqlite_module_imports'), None)
        covered = (sqlite is not None and str(native / 'run_sqlite_tests.ps1').replace('\\', '/').lower()
                   in [str(p).replace('\\', '/').lower() for p in sqlite['command']])
        if covered:
            require(all('sqlite fixture ' + name + ' ok' in text for name in ('old_xlang_sqlite_api', 'python_sqlite3_api')), 'API2 raw results missing')
        else:
            for name in ('old_xlang_sqlite_api', 'python_sqlite3_api'):
                phase(name, [RELEASE / 'xlang3.exe', native / (name + '.py'), RELEASE / 'modules'], 120,
                      normalized((native / (name + '.out')).read_bytes()))
        record['api_checks'] = dict(passed=True, cases=['old_xlang_sqlite_api', 'python_sqlite3_api'],
            covered_by='ctest-all55/xlang3_cli_sqlite_module_imports' if covered else 'standalone-api2')
        stable()
        record.update(correctness_passed=True, passed=True, status='correctness_passed_performance_pending')
    except BaseException as error:
        record.update(passed=False, status='failed_or_invalid_correctness', error=repr(error))
    finally:
        record.update(terminal=True, hashes_after={p: sha(p) if Path(p).is_file() else None for p in pins},
            finished_unix=time.time())
        record['hashes_unchanged'] = record['hashes_after'] == pins
        if not record['hashes_unchanged']:
            record.update(passed=False, correctness_passed=False, status='invalid_hash_drift')
        save()
    print(record['status'], sha(output), flush=True)
    return 0 if record['passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
