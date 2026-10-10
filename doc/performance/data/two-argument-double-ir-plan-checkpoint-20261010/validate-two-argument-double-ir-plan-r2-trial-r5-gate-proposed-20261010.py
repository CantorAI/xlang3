"""Root-only fixed gate after exact R4 correctness; no correctness repeat or build."""
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
REFERENCE = DATA / 'two-argument-double-ir-plan-r3-reference-20261010.json'
REFERENCE_SHA = '982014bca87d3712a170c0fcc455701555746864cb614ecd08afd27dd092fe58'
CONTROL_SHA = 'aa2d9f715ad11e78ffa58024c10631c37e7d83d816c4a8cb0523f6469287547f'
CORRECTNESS = DATA / 'two-argument-double-ir-plan-r6-correctness-20261010.json'
CORRECTNESS_SHA = '7a801dbc1c39e3156d24096a2f2fec80e7857519808ce313b65db99392346e85'
R4 = ROOT / 'scratch/performance/validate-two-argument-double-ir-plan-r2-trial-r4-proposed-20261010.py'
R4_SHA = 'cb4ab8ceba90b041b50d75326fc9af31e4a029672ec6a723b17d1c4cc5652de0'
WORKER_PROOF = DATA / 'two-argument-ir-known-dormant-worker-12776-20261010-proof.json'
WORKER_PROOF_SHA = '4be3d5c129a42bc093cde60d6682a562b9f90a2be590fb0061c4e29b53607cd6'
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


def bind_worker_proof(path, expected_sha, helper, pin):
    require(Path(path).resolve() == WORKER_PROOF.resolve() and expected_sha == WORKER_PROOF_SHA,
            'Only the reviewed exact worker proof may be admitted')
    proof = document(pin(path, expected_sha))
    require(proof['schema'] == 'numeric-ir-dormant-msbuild-proof-v1' and proof['terminal']
            and proof['passed'] and proof['helper_sha256'] == WATCH_SHA, 'Invalid worker proof')
    worker, children = proof['worker'], proof['children']
    require(set(worker) == set(helper.FIELDS) and worker['Name'].lower() == 'msbuild.exe'
            and worker['ProcessId'] == 12776 and worker['ParentProcessId'] == 34884
            and '/nodemode:1' in worker['CommandLine'] and '/nodeReuse:true' in worker['CommandLine'],
            'Unexpected reusable worker identity')
    require(len({p['ProcessId'] for p in children}) == len(children)
            and all(set(p) == set(helper.FIELDS) and p['Name'].lower() == 'conhost.exe'
                    and p['ParentProcessId'] == worker['ProcessId'] for p in children), 'Invalid console identities')
    for process in [worker, *children]:
        birth(process['CreationDate'])
        for field in ('KernelModeTime', 'UserModeTime'):
            require(type(process[field]) is int and process[field] >= 0, 'CPU ticks must be exact nonnegative integers')
    observations = proof['observations']
    require(len(observations) == 2 and birth(observations[1]['observed_utc'])
            - birth(observations[0]['observed_utc']) >= 2, 'Two separated raw observations required')
    for observation in observations:
        rows = document(pin(observation['raw_path'], observation['raw_sha256']))
        rows = [rows] if isinstance(rows, dict) else rows
        require(isinstance(rows, list) and len({p['ProcessId'] for p in rows}) == len(rows), 'Invalid raw process inventory')
        found = next((p for p in rows if p['ProcessId'] == worker['ProcessId']), None)
        require(found is not None and all(found.get(k) == worker[k] for k in helper.FIELDS), 'Raw worker ticks/identity mismatch')
        require(not any(p['ProcessId'] == worker['ParentProcessId'] for p in rows), 'Worker parent was present')
        direct = [p for p in rows if p['ParentProcessId'] == worker['ProcessId']]
        require({p['ProcessId'] for p in direct} == {p['ProcessId'] for p in children}
                and all(all(next(p for p in direct if p['ProcessId'] == child['ProcessId']).get(k) == child[k]
                            for k in helper.FIELDS) for child in children), 'Raw console set/ticks mismatch')
        require(not any(p['ParentProcessId'] in {c['ProcessId'] for c in children} for p in rows), 'Console descendants were present')
    require(helper._admission is None, 'CPU admission baseline already set')
    # Set precisely once from the frozen observations. Never sample a new CPU
    # baseline and never call admit_dormant_worker between phases.
    helper._admission = dict(worker=worker, children=children, exception_used=True,
                             status='hash_pinned_dormant_worker_baseline', proof_sha256=expected_sha,
                             observations=observations, scope=proof['scope'])
    return helper._admission


class Activity:
    """One-second CIM records, creation-pinned owned descendants, no dormant allowance."""
    def __init__(self, helper, prefix, name, timed):
        self.helper, self.timed = helper, timed
        self.path = DATA / (prefix + '-' + name + '.external-process-observations.jsonl')
        self.stream = self.path.open('xb')
        self.known, self.overlaps, self.errors, self.timing_overlaps = {}, [], [], []
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
        survivors = [p for p in rows if manager_only and identity(p) in self.known
                     and identity(p) != identity(self.manager)]
        build, runtime, own_ctest, dormant = [], [], [], []
        for p in rows:
            name = p['Name'].lower()
            own = identity(p) in allowed
            if name in self.helper.TOOLS:
                if name == 'msbuild.exe' and self.helper._same_dormant(p, rows):
                    dormant.append(p)
                elif name == 'ctest.exe' and own and not manager_only:
                    own_ctest.append(p)
                else:
                    build.append(p)
            elif name.startswith(('python', 'xlang3')) and not own:
                runtime.append(p)
        fatal = (build + runtime + own_ctest if self.timed else []) + survivors
        return dict(foreign_build_processes=build, foreign_runtime_processes=runtime,
                    dormant_worker_rows=dormant,
                    owned_untimed_ctest=own_ctest, invalidating_processes=fatal,
                    owned_child_survivors=survivors,
                    timing_invalidating_processes=build + runtime + own_ctest)

    def observe(self, manager_only=False, rows=None):
        row = {'observed_unix': time.time()}
        try:
            row.update(self.classify(self.helper.scan() if rows is None else rows, manager_only))
            if row['timing_invalidating_processes']:
                self.timing_overlaps.append(row)
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
        # Console hosts can outlive the primary process by a short OS teardown
        # interval. Observe and wait only for known conhost lifetimes; never kill
        # them, exempt foreign activity, or accept a still-live owner at the end.
        console_wait = []
        deadline = time.monotonic() + 10
        try:
            while True:
                rows = self.helper.scan()
                self.classify(rows)  # Discover still-live owned descendants first.
                survivors = self.classify(rows, manager_only=True)['owned_child_survivors']
                if not survivors or any(p['Name'].lower() != 'conhost.exe' for p in survivors):
                    break
                console_wait.append(survivors)
                self.observe(rows=rows)
                if time.monotonic() >= deadline:
                    break
                time.sleep(.25)
        except BaseException as error:
            self.errors.append({'cleanup_scan_error': repr(error)})
        post_ok = self.observe(manager_only=True)
        self.stream.close()
        return dict(log=self.path.name, sha256=sha(self.path), poll_interval_seconds=1,
                    overlaps=self.overlaps, scanner_errors=self.errors, post_guard_passed=post_ok,
                    observation_valid=not self.overlaps and not self.errors,
                    timing_overlaps=self.timing_overlaps,
                    measurement_valid=self.timed and not self.timing_overlaps and not self.errors,
                    owned_console_cleanup_wait=console_wait, console_wait_cap_seconds=10,
                    timing_accepted=False, owning_child_identities=list(self.known.values()),
                    dormant_worker_admission=self.helper._admission,
                    scope='Strict timed build/test/runtime policy except the single immutable, hash-pinned dormant worker while helper._same_dormant proves its original CPU ticks and identity unchanged. All other activity, survivors and scanner errors invalidate timing.',
                    limit='A short-lived external process entirely between one-second snapshots may be missed.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('gate',), default='gate')
    parser.add_argument('--application', type=Path, required=True)
    parser.add_argument('--application-sha256', required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--build-sha256', required=True)
    parser.add_argument('--application-controller', type=Path, required=True)
    parser.add_argument('--build-controller', type=Path, required=True)
    parser.add_argument('--semantic-receipt', type=Path, required=True)
    parser.add_argument('--semantic-sha256', required=True)
    parser.add_argument('--correctness-receipt', type=Path)
    parser.add_argument('--correctness-sha256')
    parser.add_argument('--known-worker-proof', type=Path)
    parser.add_argument('--known-worker-sha256')
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
        app = document(pin(args.application, args.application_sha256))
        build = document(pin(args.build, args.build_sha256))
        require(app['terminal'] and app['passed'] and app['status'] == 'applied' and app['head'] == HEAD, 'Application not passed')
        require(build['terminal'] and build['passed'] and build['exit_code'] == 0
                and build['status'] == 'build_passed' and build['sources_unchanged']
                and build['owned_child_cleanup_completed'], 'Build not passed/clean')
        require(build['application_sha256'] == args.application_sha256
                and Path(build['application_path']).resolve() == args.application.resolve(), 'Build application mismatch')
        pin(args.application_controller, app['controller_sha256'])
        pin(args.build_controller, build['controller_sha256'])
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
        proposal_path = pin(app['proposal_path'], app['proposal_sha256'])
        proposal = document(proposal_path)
        changed = set(proposal['owned_engine_paths']) | {'tests/run_fixtures.py', 'tests/run_fixtures.ps1'}
        require(set(app['allowed_changed_source_paths']) == changed and set(app['added_source_paths']) == added, 'Owned source subset mismatch')
        require({p for p in parent_sources if parent_sources[p] != sources[p]} == changed, 'Unexpected changed source')
        require(set(proposal['input_sha256']) == set(proposal['owned_engine_paths'])
                and all(parent_sources[p] == h for p, h in proposal['input_sha256'].items()), 'Merged proposal parent subset mismatch')
        require(all(sources[p] == h for p, h in proposal['candidate_sha256'].items()), 'Engine candidates mismatch')
        require(sources[FIXTURE.relative_to(ROOT).as_posix()] == proposal['fixture_sha256']
                and sources[EXPECTED.relative_to(ROOT).as_posix()] == proposal['proposed_expected_sha256'], 'Fixture bytes mismatch')
        pin(ROOT / proposal['patch'], proposal['patch_sha256'])
        require(Path(app['trial_patch_path']).resolve() == (ROOT / proposal['patch']).resolve()
                and app['trial_patch_sha256'] == proposal['patch_sha256'], 'Application merged patch mismatch')
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
        semantic = document(pin(args.semantic_receipt, args.semantic_sha256))
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
        require(helper._admission is None, 'Policy module already has an admission')
        require(bool(args.known_worker_proof) == bool(args.known_worker_sha256), 'Worker proof path/SHA must be supplied together')
        record['known_worker_admission'] = (bind_worker_proof(args.known_worker_proof,
            args.known_worker_sha256, helper, pin) if args.known_worker_proof else None)
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
                      proposal_path=str(proposal_path), proposal_sha256=app['proposal_sha256'],
                      candidate_semantic_sha256=args.semantic_sha256, cpython_reference_sha256=REFERENCE_SHA,
                      fixture_inventory=dict(core=407, compat=11, expected_failures=3), hashes_before=dict(pins))
        stable()
        save()
        require(args.correctness_receipt is not None and args.correctness_sha256 == CORRECTNESS_SHA
                and args.correctness_receipt.resolve() == CORRECTNESS.resolve(), 'Exact fresh R4 correctness receipt required')
        prior = document(pin(args.correctness_receipt, args.correctness_sha256))
        pin(R4, R4_SHA)
        require(prior['terminal'] and prior['passed'] and prior['correctness_passed']
                and prior['mode'] == 'correctness' and prior['status'] == 'correctness_passed_gate_pending'
                and prior['hashes_unchanged'] and prior['controller_sha256'] == R4_SHA, 'R4 correctness not passed')
        require(prior['application_sha256'] == args.application_sha256 and prior['build_sha256'] == args.build_sha256
                and prior['source_sha256'] == sources and prior['release_sha256'] == release
                and prior['fixed_baseline_sha256'] == baseline and prior['test_input_names'] == record['test_input_names'], 'Correctness candidate/input mismatch')
        require(prior['candidate_semantic_sha256'] == args.semantic_sha256
                and prior['proposal_sha256'] == app['proposal_sha256']
                and [r['name'] for r in prior['phases']] == ['python-fixtures', 'ctest-inventory', 'ctest-all55']
                and len(set(prior['ctest_actual_passed_names'])) == 55 and prior['api_checks']['passed'],
                'Complete current all55/API2 proof required')
        verify(Path(), prior['hashes_before'])
        require(prior['hashes_before'] == prior['hashes_after'], 'Prior correctness input drift')
        for row in prior['phases']:
            require(row['passed'] and row['hashes_unchanged'] and row['owned_child_cleanup_completed']
                    and not row['timed'] and not row.get('inherited_semantics'), 'Prior fresh correctness phase failed')
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
