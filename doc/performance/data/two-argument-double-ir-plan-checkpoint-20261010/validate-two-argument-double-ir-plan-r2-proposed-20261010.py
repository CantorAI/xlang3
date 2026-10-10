"""Root-only numeric IR correctness and fixed gate; no build, retries or old suite reuse."""
import argparse
from datetime import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import time

ROOT = Path('D:/CantorAI/xlang3')
CP = Path('C:/Python/Python314/python.exe')
BUILD_DIR = ROOT / 'build-repro/main-verify-20261006'
RELEASE = BUILD_DIR / 'Release'
BASELINE = ROOT / 'build-repro/Release'
DATA = ROOT / 'doc/performance/data'
CTEST = Path('C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe')
WATCH = ROOT / 'scratch/performance/gc-phase-dormant-msbuild-activity-watch-20261009.py'
WATCH_SHA = '6dea8abf4f66ff0d2a95830e4c560202359c57502f0f635ecf9e1dda5494bd48'
GATE = ROOT / 'benchmarks/check_regression.py'
GATE_SHA = '579934598caddc37409fed03181b0cd42408853a34bde65dff48b777e4e8ffef'
APPLIER = ROOT / 'scratch/performance/apply-build-two-argument-ir-trial-20261010.py'
APPLIER_SHA = '5c4a908179c03658b7462748df99b6a7d01103d01c9bdf23ab0c1e7c6b3d172d'
PROOF = ROOT / 'scratch/performance/two-argument-double-ir-plan-engine-r2-fixture-r3-proposed-20261009-provenance.json'
PROOF_SHA = '6a6a7a42f6564e0905454206060c9a25449c0ee89e45050618ecb73a38eca26f'
REFERENCE = DATA / 'two-argument-double-ir-plan-r3-reference-20261010.json'
REFERENCE_SHA = '982014bca87d3712a170c0fcc455701555746864cb614ecd08afd27dd092fe58'
SEMANTIC = DATA / 'two-argument-double-ir-plan-r2-candidate-semantic-20261010.json'
SEMANTIC_SHA = '606237d0aad9954aaef56050e134eca4f99d32b775c8d5883407a884d153d588'
CONTROL_SHA = 'aa2d9f715ad11e78ffa58024c10631c37e7d83d816c4a8cb0523f6469287547f'
HEAD = '46496cf5ddb903e25927cce616c29c2f81032566'
FIXTURE = ROOT / 'tests/fixtures/core/two_argument_double_ir_plan.py'
EXPECTED = ROOT / 'tests/fixtures/expected/two_argument_double_ir_plan.out'
CASES = ('local_slots', 'scalar_arithmetic', 'range_for', 'function_calls',
         'class_construct', 'list_append', 'property_access', 'deepcopy_memo',
         'json_dumps', 'gc_traversal', 'subparsers')
TEST_SUFFIXES = {'.py', '.ps1', '.out', '.cpp', '.h', '.c', '.json', '.toml', '.txt', '.x', '.xlang'}


def require(value, message):
    if not value:
        raise RuntimeError(message)


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def document(path):
    return json.loads(Path(path).read_bytes())


def tree(path):
    return {p.relative_to(path).as_posix(): sha(p) for p in sorted(path.rglob('*')) if p.is_file()}


def normalized(raw):
    return raw.decode('utf-8').replace('\r\n', '\n').rstrip()


def birth(value):
    match = re.fullmatch(r'/Date\((-?\d+)(?:[+-]\d{4})?\)/', value or '')
    return int(match.group(1)) / 1000 if match else datetime.fromisoformat(value.replace('Z', '+00:00')).timestamp()


def identity(row):
    return (int(row['ProcessId']), row['CreationDate'])


class Activity:
    """One-second CIM records, creation-pinned owned descendants, no dormant allowance."""
    def __init__(self, helper, prefix, name, timed):
        self.helper, self.timed = helper, timed
        self.path = DATA / (prefix + '-' + name + '.external-process-observations.jsonl')
        self.stream = self.path.open('xb')
        self.known, self.overlaps, self.errors = {}, [], []
        self.stop = threading.Event()
        self.thread = None
        rows = helper.scan()
        manager = next(p for p in rows if p['ProcessId'] == os.getpid())
        self.manager = manager
        self.known[identity(manager)] = manager

    def classify(self, rows, manager_only=False):
        manager = next((p for p in rows if p['ProcessId'] == os.getpid()), None)
        require(manager is not None and identity(manager) == identity(self.manager), 'Manager lifetime changed')
        if not manager_only:
            live = {p['ProcessId']: p for p in rows if identity(p) in self.known}
            changed = True
            while changed:
                changed = False
                for p in rows:
                    if identity(p) in self.known or p['ParentProcessId'] not in live:
                        continue
                    parent = live[p['ParentProcessId']]
                    # A child older than the current parent belongs to a prior PID lifetime.
                    if birth(p['CreationDate']) < birth(parent['CreationDate']):
                        continue
                    self.known[identity(p)] = p
                    live[p['ProcessId']] = p
                    changed = True
        allowed = {identity(self.manager)} if manager_only else set(self.known)
        build, runtime, own_ctest = [], [], []
        for p in rows:
            name = p['Name'].lower()
            own = identity(p) in allowed
            if name in self.helper.TOOLS:
                if name == 'ctest.exe' and own and not manager_only:
                    own_ctest.append(p)
                else:
                    build.append(p)
            elif name.startswith(('python', 'xlang3')) and not own:
                runtime.append(p)
        foreign_ctest = [p for p in build if p['Name'].lower() == 'ctest.exe']
        fatal = build + runtime + own_ctest if self.timed else foreign_ctest + runtime
        return dict(foreign_build_processes=build, foreign_runtime_processes=runtime,
                    owned_untimed_ctest=own_ctest, invalidating_processes=fatal)

    def observe(self, manager_only=False):
        row = {'observed_unix': time.time()}
        try:
            row.update(self.classify(self.helper.scan(), manager_only))
            if row['invalidating_processes']:
                self.overlaps.append(row)
        except BaseException as error:
            row['scanner_error'] = repr(error)
            self.errors.append(row)
        self.stream.write((json.dumps(row) + '\n').encode())
        self.stream.flush()
        return not row.get('invalidating_processes') and not row.get('scanner_error')

    def launch_watch(self):
        def loop():
            while not self.stop.wait(1):
                self.observe()
        self.thread = threading.Thread(target=loop, daemon=True)
        self.thread.start()

    def finish(self):
        self.stop.set()
        if self.thread is not None:
            self.thread.join(timeout=12)
            if self.thread.is_alive():
                self.errors.append({'error': 'watcher_join_timeout'})
        post_ok = self.observe(manager_only=True)
        self.stream.close()
        return dict(log=self.path.name, sha256=sha(self.path), poll_interval_seconds=1,
                    overlaps=self.overlaps, scanner_errors=self.errors, post_guard_passed=post_ok,
                    observation_valid=not self.overlaps and not self.errors,
                    timing_accepted=False, owning_child_identities=list(self.known.values()),
                    scope='Untimed external builds are recorded; foreign CTest/runtime and scanner errors fail correctness. Timed phases reject every build/test/runtime overlap; no dormant exemption.',
                    limit='A short-lived external process entirely between one-second snapshots may be missed.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('correctness', 'gate'), required=True)
    parser.add_argument('--application', type=Path, required=True)
    parser.add_argument('--application-sha256', required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--build-sha256', required=True)
    parser.add_argument('--correctness-receipt', type=Path)
    parser.add_argument('--correctness-sha256')
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    require(sys.version_info[:3] == (3, 14, 7) and sys.flags.optimize == 0, 'Exact unoptimized CPython3.14.7 required')
    require(Path(sys.executable).resolve() == CP.resolve(), 'Wrong controller executable')
    require(re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9_.-]+', args.prefix), 'Unsafe output prefix')
    sys.dont_write_bytecode = True
    output = DATA / (args.prefix + '.json')
    require(not output.exists(), 'Output receipt already exists')
    pins = {}
    record = dict(terminal=False, passed=False, mode=args.mode, status='preflight',
                  controller_sha256=sha(__file__), phases=[], correctness_passed=False,
                  fixed_gate_passed=False, official_benchmark_completed=False,
                  scope='Fresh candidate validation only; no official benchmark or speed claim.')
    sources = release = baseline = None

    def save():
        output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')

    def pin(path, expected=None):
        path = Path(path).resolve(strict=True)
        value = sha(path)
        require(expected is None or value == expected, 'Input hash mismatch: ' + str(path))
        require(str(path) not in pins or pins[str(path)] == value, 'Inconsistent input hash')
        pins[str(path)] = value
        return path

    def verify(root, mapping):
        for p, h in mapping.items():
            pin(root / p, h)

    def stable():
        require(all(sha(p) == h for p, h in pins.items()), 'Input bytes changed')
        require(tree(RELEASE) == release and tree(BASELINE) == baseline, 'Release/baseline set or bytes changed')
        require(subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT).decode().strip() == HEAD, 'HEAD changed')
        require(test_names() == record['test_input_names'], 'Test input set changed')

    def test_names():
        return sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / 'tests').rglob('*')
                      if p.is_file() and p.suffix in TEST_SUFFIXES and '__pycache__' not in p.parts)

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
        app = document(pin(args.application, args.application_sha256))
        build = document(pin(args.build, args.build_sha256))
        require(app['terminal'] and app['passed'] and app['status'] == 'applied' and app['head'] == HEAD, 'Application not passed')
        require(build['terminal'] and build['passed'] and build['exit_code'] == 0
                and build['status'] == 'build_passed' and build['sources_unchanged']
                and build['owned_child_cleanup_completed'], 'Build not passed/clean')
        require(build['application_sha256'] == args.application_sha256
                and Path(build['application_path']).resolve() == args.application.resolve(), 'Build application mismatch')
        pin(APPLIER, APPLIER_SHA)
        require(app['controller_sha256'] == build['controller_sha256'] == APPLIER_SHA, 'Unexpected app/build producer')
        control_path = pin(app['accepted_control_manifest_path'], CONTROL_SHA)
        require(app['accepted_control_manifest_sha256'] == CONTROL_SHA, 'Wrong accepted control')
        control = document(control_path)
        require(control['terminal'] and control['full_validated'] and control['fixed_gate_passed'], 'Accepted parent not validated')
        parent_sources = dict(control['source_sha256'])
        require(len(parent_sources) == 143, 'Unexpected parent source count')
        parent_sources.update(app['harness_overlay_sha256'])
        require(parent_sources == app['before_source_sha256'], 'Parent/source overlay mismatch')
        sources = app['source_sha256']
        added = {'tests/fixtures/core/two_argument_double_ir_plan.py', 'tests/fixtures/expected/two_argument_double_ir_plan.out'}
        require(app['source_count'] == len(sources) == 145 and set(sources) == set(parent_sources) | added, 'Source145 union mismatch')
        proposal = document(pin(PROOF, PROOF_SHA))
        require(app['proposal_sha256'] == PROOF_SHA, 'Unexpected proposal')
        changed = set(proposal['owned_engine_paths']) | {'tests/run_fixtures.py', 'tests/run_fixtures.ps1'}
        require(set(app['allowed_changed_source_paths']) == changed and set(app['added_source_paths']) == added, 'Owned source subset mismatch')
        require({p for p in parent_sources if parent_sources[p] != sources[p]} == changed, 'Unexpected changed source')
        require(all(sources[p] == h for p, h in proposal['candidate_sha256'].items()), 'Engine candidates mismatch')
        require(sources[FIXTURE.relative_to(ROOT).as_posix()] == proposal['fixture_sha256']
                and sources[EXPECTED.relative_to(ROOT).as_posix()] == proposal['proposed_expected_sha256'], 'Fixture bytes mismatch')
        pin(ROOT / proposal['patch'], proposal['patch_sha256'])
        verify(ROOT, sources)
        verify(ROOT, app['unowned_tracked_dirty_sha256'])
        verify(ROOT, app['protected_test_input_sha256'])
        release, baseline = build['release_sha256'], app['fixed_baseline_sha256']
        require(build['source_sha256'] == sources and len(release) == 178 and len(baseline) == 177, 'Build/map mismatch')
        require(baseline == control['fixed_baseline_sha256'], 'Fixed baseline changed')
        verify(RELEASE, release)
        verify(BASELINE, baseline)
        verify(control_path.parent / 'sources', control['source_sha256'])
        require(tree(control_path.parent / 'Release') == control['release_sha256'], 'Preserved parent Release changed')
        pin(DATA / build['log'], build['log_sha256'])
        require(build['command'][:3] == ['cmd.exe', '/d', '/c'], 'Unexpected build command')
        pin(build['command'][3], build['build_command_source_sha256'])
        pin(CP)
        pin(CP.parent / 'python314.dll')
        reference = document(pin(REFERENCE, REFERENCE_SHA))
        require(reference['terminal'] and reference['status'] == 'semantic_reference_passed' and reference['inputs_unchanged'], 'CP reference failed')
        require(reference['phases'][0]['name'] == 'cpython3147' and reference['phases'][0]['passed']
                and reference['phases'][0]['exit_code'] == 0, 'CP phase failed')
        for name in ('python.exe', 'python314.dll'):
            path = CP.parent / name
            require(reference['pins'][str(path)] == pins[str(path.resolve())], 'CP reference executable changed')
        cp_stdout = pin(DATA / 'two-argument-double-ir-plan-r3-reference-20261010-cpython3147.stdout.log',
                        reference['phases'][0]['stdout_sha256'])
        cp_stderr = pin(DATA / 'two-argument-double-ir-plan-r3-reference-20261010-cpython3147.stderr.log',
                        reference['phases'][0]['stderr_sha256'])
        require(normalized(cp_stdout.read_bytes()) == normalized(EXPECTED.read_bytes())
                and cp_stderr.read_bytes() == b'', 'CP reference raw output mismatch')
        semantic = document(pin(SEMANTIC, SEMANTIC_SHA))
        require(semantic['terminal'] and semantic['passed'] and semantic['inputs_unchanged'] and semantic['exit_code'] == 0
                and semantic['application_sha256'] == args.application_sha256 and semantic['build_sha256'] == args.build_sha256,
                'Fresh candidate semantic result mismatch')
        require(list(map(Path, semantic['command'])) == [RELEASE / 'xlang3.exe', FIXTURE], 'Semantic command mismatch')
        verify(Path(), semantic['pins'])
        stdout = pin(DATA / semantic['stdout'], semantic['stdout_sha256'])
        stderr = pin(DATA / semantic['stderr'], semantic['stderr_sha256'])
        require(normalized(stdout.read_bytes()) == normalized(EXPECTED.read_bytes()) and stderr.read_bytes() == b'', 'Candidate semantic output mismatch')
        require(reference['phases'][0]['stdout_sha256'] == semantic['stdout_sha256']
                and reference['phases'][0]['stderr_sha256'] == semantic['stderr_sha256'], 'CP/current signature mismatch')
        pin(CTEST)
        pin(BUILD_DIR / 'CTestTestfile.cmake')
        pin(ROOT / 'CMakeLists.txt')
        pin(GATE, GATE_SHA)
        for case in CASES:
            pin(ROOT / 'benchmarks/cases' / (case + '.py'))
        pin(WATCH, WATCH_SHA)
        spec = importlib.util.spec_from_file_location('numeric_trial_strict_scan', WATCH)
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        require(helper._admission is None, 'Dormant worker exemption forbidden')
        record['test_input_names'] = test_names()
        for name in record['test_input_names']:
            pin(ROOT / name)
        runner = (ROOT / 'tests/run_fixtures.py').read_text(encoding='utf-8')
        core = re.search(r'CORE_CASES\s*=\s*"""(.*?)"""\.split\(\)', runner, re.S).group(1).split()
        sections = re.search(r'SECTION_CASES\s*=\s*"""(.*?)"""\.split\(\)', runner, re.S).group(1).split()
        require(len(core) == len(set(core)) == 407 and len(sections) == 11
                and core.count('two_argument_double_ir_plan') == 1, 'Fixture inventory changed')
        record.update(application_sha256=args.application_sha256, build_sha256=args.build_sha256,
                      source_sha256=sources, source_count=145, release_sha256=release, fixed_baseline_sha256=baseline,
                      unowned_tracked_dirty_sha256=app['unowned_tracked_dirty_sha256'],
                      candidate_semantic_sha256=SEMANTIC_SHA, cpython_reference_sha256=REFERENCE_SHA,
                      fixture_inventory=dict(core=407, compat=11, expected_failures=3), hashes_before=dict(pins))
        stable()
        save()
        if args.mode == 'correctness':
            phase('python-fixtures', [CP, '-I', ROOT / 'tests/run_fixtures.py', RELEASE / 'xlang3.exe'], 1200, '')
            ctest = [CTEST, '--test-dir', BUILD_DIR, '-C', 'Release']
            inventory = json.loads(phase('ctest-inventory', [*ctest, '--show-only=json-v1'], 120))
            names = [t['name'] for t in inventory['tests']]
            require(len(names) == len(set(names)) == 55, 'Expected all55 CTest inventory')
            for test in inventory['tests']:
                command = test['command']
                require(not any('/Debug/' in str(part).replace('\\', '/') for part in command), 'Debug CTest command')
                for part in command:
                    if Path(str(part)).name.lower().startswith('python') and str(part).lower().endswith('.exe'):
                        require(Path(part).resolve() == CP.resolve(), 'CTest selected another CPython')
            record['ctest_inventory'] = inventory
            raw = phase('ctest-all55', [*ctest, '--output-on-failure', '--verbose', '--parallel', '1'], 1800)
            text = normalized(raw)
            passed = re.findall(r'Test\s+#\d+:\s+(\S+)\s+\.+\s+Passed', text)
            require(len(passed) == 55 and set(passed) == set(names)
                    and '100% tests passed, 0 tests failed out of 55' in text, 'CTest actual results mismatch')
            record['ctest_actual_passed_names'] = passed
            sqlite = next((t for t in inventory['tests'] if t['name'] == 'xlang3_cli_sqlite_module_imports'), None)
            native = ROOT / 'tests/native/sqlite'
            covered = (sqlite is not None and str(native / 'run_sqlite_tests.ps1').replace('\\', '/').lower()
                       in [str(p).replace('\\', '/').lower() for p in sqlite['command']])
            if covered:
                require(all('sqlite fixture ' + name + ' ok' in text for name in ('old_xlang_sqlite_api', 'python_sqlite3_api')), 'Covered API2 raw results missing')
                record['api_checks'] = dict(passed=True, cases=['old_xlang_sqlite_api', 'python_sqlite3_api'],
                                            covered_by='ctest-all55/xlang3_cli_sqlite_module_imports')
            else:
                for name in ('old_xlang_sqlite_api', 'python_sqlite3_api'):
                    phase(name, [RELEASE / 'xlang3.exe', native / (name + '.py'), RELEASE / 'modules'], 120,
                          normalized((native / (name + '.out')).read_bytes()))
                record['api_checks'] = dict(passed=True, cases=['old_xlang_sqlite_api', 'python_sqlite3_api'], covered_by='standalone-api2')
            record.update(correctness_passed=True, status='correctness_passed_gate_pending', passed=True)
        else:
            require(args.correctness_receipt is not None and args.correctness_sha256, 'Current correctness receipt required before gate')
            prior = document(pin(args.correctness_receipt, args.correctness_sha256))
            require(prior['terminal'] and prior['passed'] and prior['correctness_passed']
                    and prior['mode'] == 'correctness' and prior['status'] == 'correctness_passed_gate_pending'
                    and prior['hashes_unchanged'] and prior['controller_sha256'] == sha(__file__), 'Current controller correctness not passed')
            require(prior['application_sha256'] == args.application_sha256 and prior['build_sha256'] == args.build_sha256
                    and prior['source_sha256'] == sources and prior['release_sha256'] == release
                    and prior['fixed_baseline_sha256'] == baseline and prior['test_input_names'] == record['test_input_names'], 'Correctness candidate/input mismatch')
            verify(Path(), prior['hashes_before'])
            require(prior['hashes_before'] == prior['hashes_after'], 'Prior correctness input drift')
            for row in prior['phases']:
                require(row['passed'] and row['hashes_unchanged'] and row['owned_child_cleanup_completed'] and not row['timed'], 'Prior correctness phase failed')
                pin(DATA / row['stdout'], row['stdout_sha256'])
                pin(DATA / row['stderr'], row['stderr_sha256'])
                pin(DATA / row['external_process_watch']['log'], row['external_process_watch']['sha256'])
            record.update(correctness_passed=True, correctness_receipt_sha256=args.correctness_sha256,
                          correctness_scope='Same candidate fresh full checks authenticated from correctness mode; no parent correctness reuse.')
            record['hashes_before'] = dict(pins)
            save()
            gate = DATA / (args.prefix + '-fixed-gate.json')
            require(not gate.exists(), 'Gate JSON already exists')
            phase('fixed-gate', [CP, '-I', GATE, '--baseline', BASELINE / 'xlang3.exe',
                  '--candidate', RELEASE / 'xlang3.exe', '--output', gate,
                  '--repeats', '21', '--warmup', '5', '--threshold', '0.10'], 1800, timed=True)
            report = document(gate)
            require(report['status'] == 'pass' and set(report['cases']) == set(CASES)
                    and report['repeats'] == 21 and report['warmup'] == 5 and report['threshold'] == .10,
                    'Default11 gate not passed')
            require(all(v['status'] == 'pass' and all(len(a['baseline_seconds']) == len(a['candidate_seconds']) == 21
                        for a in v['attempts']) for v in report['cases'].values()), 'Gate arrays incomplete')
            record.update(fixed_gate_passed=True, fixed_gate_sha256=sha(gate), fixed_gate_path=gate.name,
                          passed=True, status='correctness_and_fixed_gate_passed_no_official_benchmark')
        stable()
    except BaseException as error:
        record.update(passed=False, status='failed_or_invalid_' + args.mode, error=repr(error))
    finally:
        record['terminal'] = True
        record['hashes_after'] = {p: sha(p) if Path(p).is_file() else None for p in pins}
        record['hashes_unchanged'] = all(record['hashes_after'][p] == h for p, h in pins.items())
        if not record['hashes_unchanged']:
            record.update(passed=False, fixed_gate_passed=False, status='terminal_invalid_input_drift')
        record['finished_unix'] = time.time()
        save()
    print(record['status'], str(output), sha(output), flush=True)
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
