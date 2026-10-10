"""Resumable, independently guarded official all-97 ledger; root execution only.

The unchanged shimmed runner receives one original definition per invocation.
No valid finished definition is repeated, including a genuine benchmark failure.
Only invalid attempts may be attempted again on a later explicit resume, at most
three child launches per definition. No automatic within-session retries occur.
"""
from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import importlib.metadata
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import threading

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CANDIDATE = RELEASE / 'xlang3.exe'
BASELINE = ROOT / 'build-repro/Release'
CONTROL = ROOT / 'build-repro/controls/gc-generic-cycles-r7b-accepted-20261009'
SITE = ROOT / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'
BENCHMARK_ROOT = CP.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks'
RUNNER = ROOT / 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py'
RUNNER_SHA = 'c078ee77d5d66165f0bf4b2d782df56e71868ada827c81c5c53516c8d017ceaf'
HOOK = ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
HOOK_SHA = '2ffc217fe20497156bfcb1cebc7fb87203f33c6d4d3dbb6ecbcbcf91ea4cb317'
SUMMARY = ROOT / 'benchmarks/diagnostics/summarize_pyperformance_comparison.py'
SUMMARY_SHA = 'c067fcea41ea66be0249209bb86e1ca06c8b9e93b6696076a69d0f0e9aa76255'
PARTIAL = RUNNER.with_name('preserve_pyperformance_partial.py')
PARTIAL_SHA = '0dfbc86863421165ecfda246b17dcc4846a41306a49aaf346456b78273bf7c06'
WATCH = ROOT / 'scratch/performance/gc-phase-dormant-msbuild-activity-watch-20261009.py'
WATCH_SHA = '6dea8abf4f66ff0d2a95830e4c560202359c57502f0f635ecf9e1dda5494bd48'
CANONICAL = DATA / 'pyperformance-xlang3-live-eval-vs-cpython3147-full-fast-20261007-all-97-status.csv'
CANONICAL_SHA = '022c1774d346fa381e0b36b7fa4a8d336e7a53466bff85b6073959799d50ab77'
CP_STEM = 'pyperformance-cpython3147-live-eval-full-fast-20261007'
CP_HASHES = {'.json': 'ac474df5576ac85f6f5350363affecc439d4f658b4dfa3dbe291b46964276eb0',
    '.log': '6a1824eed3abad4cebae80356f0f600f1a8728755730d2f64c0bd7d4b3ca9e6c',
    '-provenance.json': '3d6c8cb2c72c786583c382b835e8e411f56ca285963bc2835f757a13f399f85c'}
MAX_CHILD_ATTEMPTS = 3
PROTOCOL = 'gc-r7b-per-definition-all97-v1'


def now():
    return datetime.now(timezone.utc).isoformat()


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def document(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def map_digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def load_module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def contained(directory, name):
    relative = Path(name)
    require(not relative.is_absolute() and '..' not in relative.parts, 'Unsafe relative input path')
    result = (directory / relative).resolve()
    require(result.is_relative_to(directory.resolve()), 'Input path escaped its directory')
    return result


def tree_names(directory, exclude_bytecode=False):
    return {p.relative_to(directory).as_posix() for p in directory.rglob('*') if p.is_file()
        and not (exclude_bytecode and ('__pycache__' in p.parts or p.suffix in ('.pyc', '.pyo')))}


def tree_hashes(directory, exclude_bytecode=False):
    return {name: digest(contained(directory, name)) for name in sorted(tree_names(directory, exclude_bytecode))}


def atomic_save(path, record):
    # Only the owned ledger is replaced; immutable attempt files use exclusive creation.
    temporary = path.with_suffix(path.suffix + '.writing')
    with temporary.open('xb') as stream:
        stream.write((json.dumps(record, indent=2) + '\n').encode('utf-8'))
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def write_attempt(path, row):
    with path.open('xb') as stream:
        stream.write((json.dumps(row, indent=2) + '\n').encode('utf-8'))
        stream.flush()
        os.fsync(stream.fileno())


def creation_seconds(value):
    # Windows PowerShell commonly serializes CreationDate as /Date(milliseconds)/.
    require(isinstance(value, str) and bool(value), 'Missing process creation identity')
    match = re.fullmatch(r'/Date\((-?\d+)(?:[+-]\d{4})?\)/', value)
    if match:
        return int(match.group(1)) / 1000
    return datetime.fromisoformat(value.replace('Z', '+00:00')).timestamp()


def process_identity(row):
    return {key: row.get(key) for key in ('Name', 'ProcessId', 'ParentProcessId', 'CreationDate')}


class OwnedProcesses:
    """Creation-pinned observed descendants; never a name-only runtime exemption."""
    def __init__(self, manager):
        self.manager = process_identity(manager)
        creation_seconds(self.manager['CreationDate'])
        self.known = {(self.manager['ProcessId'], self.manager['CreationDate']): self.manager}
        self.lock = threading.Lock()
        self.child_pid = None
        self.child_identity = None

    def seed_child(self, child, rows):
        # Popen is the ownership proof for the root; CIM supplies its independent lifetime identity.
        self.child_pid = child.pid
        row = next((p for p in rows if p['ProcessId'] == child.pid), None)
        if row is not None and child.poll() is None:
            require(row['ParentProcessId'] == self.manager['ProcessId'], 'Owned child parent mismatch')
            require(creation_seconds(row['CreationDate']) >= creation_seconds(self.manager['CreationDate']),
                'Owned child predates its parent')
            self.child_identity = process_identity(row)
            with self.lock:
                self.known[(row['ProcessId'], row['CreationDate'])] = self.child_identity

    def allowed(self, rows, manager_only=False):
        with self.lock:
            manager = next((p for p in rows if p['ProcessId'] == self.manager['ProcessId']), None)
            require(manager is not None and process_identity(manager) == self.manager, 'Manager PID identity changed')
            if manager_only:
                return {(self.manager['ProcessId'], self.manager['CreationDate'])}
            live = {p['ProcessId']: p for p in rows
                if (p['ProcessId'], p.get('CreationDate')) in self.known}
            changed = True
            while changed:
                changed = False
                for row in rows:
                    key = (row['ProcessId'], row.get('CreationDate'))
                    if key in self.known:
                        continue
                    parent = live.get(row['ParentProcessId'])
                    if parent is None:
                        continue
                    require(creation_seconds(row['CreationDate']) >= creation_seconds(parent['CreationDate']),
                        'Unproven descendant after parent-PID reuse')
                    self.known[key] = process_identity(row)
                    live[row['ProcessId']] = row
                    changed = True
            return set(self.known)

    def snapshot(self):
        with self.lock:
            return list(self.known.values())


def make_watcher(session, attempt, worker_pid, worker_creation):
    # A NEW helper module resets its single admission only in this untimed boundary.
    # It is never reloaded or reset while the child or watcher is active.
    watcher = load_module(WATCH, 'ledger_activity_' + session + '_' + str(attempt))
    manager = next(p for p in watcher.scan() if p['ProcessId'] == os.getpid())
    owned = OwnedProcesses(manager)
    original_classify = watcher.classify

    def classify_with_foreign_runtimes(rows, include_runtimes=False, manager_only=False):
        result = original_classify(rows, include_runtimes=False)
        allowed = owned.allowed(rows, manager_only=manager_only)
        for row in rows:
            if row['Name'].lower().startswith(('python', 'xlang3')):
                if (row['ProcessId'], row.get('CreationDate')) not in allowed:
                    # Never publish unrelated runtime command lines.
                    result['busy'].append(process_identity(row))
        return result

    watcher.classify = classify_with_foreign_runtimes
    admission = watcher.admit_dormant_worker(worker_pid) if worker_pid is not None else {
        'status': 'no_worker_exception_requested', 'exception_used': False}
    if admission['exception_used']:
        require(admission['worker']['CreationDate'] == worker_creation,
            'Known worker PID was reused or its explicit creation identity differs')
    return watcher, owned, admission, classify_with_foreign_runtimes


def authenticate(args):
    receipts, pins = {}, {}

    def track(path, expected=None):
        path = Path(path).resolve(strict=True)
        value = digest(path)
        require(expected is None or value == expected, 'Input hash mismatch: ' + str(path))
        require(str(path) not in pins or pins[str(path)] == value, 'Conflicting input pin')
        pins[str(path)] = value
        return value

    for label in ('application', 'build', 'correctness', 'performance', 'accepted_manifest'):
        path = getattr(args, label).resolve(strict=True)
        expected = getattr(args, label + '_sha256')
        require(re.fullmatch(r'[0-9a-f]{64}', expected), 'Invalid receipt hash')
        require(path.is_relative_to(ROOT.resolve()), 'Receipt is outside the workspace')
        track(path, expected)
        receipts[label] = document(path)
    app, build, correct, perf, accepted = (receipts[k] for k in
        ('application', 'build', 'correctness', 'performance', 'accepted_manifest'))
    require(app['terminal'] and app['source_count'] == len(app['source_sha256']) == 143, 'Expected source143 application')
    require(build['terminal'] and build['passed'] and build['exit_code'] == 0 and build['status'] == 'build_passed'
        and build['sources_unchanged'], 'Build is not validated')
    require(build['application_sha256'] == args.application_sha256, 'Build/application linkage differs')
    require(correct['terminal'] and correct['correctness_passed'] and correct['sources_unchanged']
        and correct['release_unchanged'] and correct['fixed_baseline_unchanged'], 'Correctness is not validated')
    require(correct['application_sha256'] == args.application_sha256 and correct['build_sha256'] == args.build_sha256,
        'Correctness linkage differs')
    require(correct['fixture_counts'] == {'core': 406, 'compat_sections': 11, 'expected_failures': 3}
        and correct['ctest_count'] == 9, 'Correctness counts differ')
    require(len(correct['phases']) == 7 and all(p['passed'] and p['exit_code'] == 0 for p in correct['phases']),
        'Incomplete correctness phases')
    require(perf['terminal'] and perf['hashes_unchanged'] and perf['status'] == 'gate_passed_original_gc_completed'
        and perf['correctness_sha256'] == args.correctness_sha256, 'Performance validation is incomplete')
    require(len(perf['phases']) == 3 and all(p['exit_code'] == 0 and p['measurement_valid']
        and p['owned_child_cleanup_completed'] for p in perf['phases']), 'Performance phase failed or invalid')
    require(accepted['terminal'] and accepted['status'] == 'preserved_fully_validated_generic_gc_r7b'
        and accepted['full_validated'] and accepted['correctness_passed'] and accepted['fixed_gate_passed']
        and accepted['affected_originals_completed'], 'Accepted checkpoint is not validated')
    require(args.accepted_manifest.resolve() == (CONTROL / 'provenance.json').resolve(), 'Wrong preserved checkpoint')
    for label in ('application', 'build', 'correctness', 'performance'):
        require(accepted[label + '_sha256'] == getattr(args, label + '_sha256'), 'Preserved receipt linkage differs')
    sources, release, baseline = app['source_sha256'], build['release_sha256'], app['fixed_baseline_sha256']
    require(sources == build['source_sha256'] == correct['source_sha256'] == accepted['source_sha256'], 'Source maps differ')
    require(release == correct['release_sha256'] == accepted['release_sha256'] and len(release) == 178, 'Release maps differ')
    require(baseline == accepted['fixed_baseline_sha256'] and len(baseline) == 177, 'Fixed baseline maps differ')
    require(app['unowned_tracked_dirty_sha256'] == accepted['unowned_tracked_dirty_sha256'], 'Unowned input map differs')
    for directory, mapping in ((ROOT, sources), (RELEASE, release), (BASELINE, baseline),
            (CONTROL / 'sources', sources), (CONTROL / 'Release', release), (ROOT, app['unowned_tracked_dirty_sha256'])):
        for name, expected in mapping.items():
            track(contained(directory, name), expected)
    for p in correct['phases'] + perf['phases']:
        for stream in ('stdout', 'stderr'):
            track(contained(DATA, p[stream]), p[stream + '_sha256'])
        if 'external_process_watch' in p:
            watch = p['external_process_watch']
            require(watch['measurement_valid'] and not watch['overlaps'] and not watch['scanner_errors'], 'Prior timing watch invalid')
            track(contained(DATA, watch['log']), watch['sha256'])
    track(contained(DATA, build['log']), build['log_sha256'])
    gate_meta = perf['fixed_gate']
    require(gate_meta['passed'] and gate_meta['exit_code'] == 0 and gate_meta['status'] == 'pass', 'Gate has not passed')
    gate_path = contained(DATA, gate_meta['output'])
    track(gate_path, gate_meta['sha256'])
    gate = document(gate_path)
    require((gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .10)
        and gate['status'] == 'pass' and len(gate['cases']) == 11
        and all(p['status'] == 'pass' for p in gate['cases'].values()), 'Gate weakened or incomplete')
    for name, row in gate['cases'].items():
        track(ROOT / 'benchmarks/cases' / (name + '.py'), row['source_sha256'])
    for label, row in perf['official'].items():
        require(label in ('cpython3147', 'xlang3') and row['completed'] and row['exit_code'] == 0, 'Original GC case incomplete')
        track(contained(DATA, row['output']), row['sha256'])
        require(set(row['scores']) == {'create_gc_cycles', 'gc_traversal'}, 'Wrong original GC definitions')
        for score in row['scores'].values():
            require(len(score['values']) == 20 and all(math.isfinite(v) and v > 0 for v in score['values']), 'Wrong GC samples')
    require(set(perf['official']) == {'cpython3147', 'xlang3'}, 'Missing runtime GC result')
    # Initialized engine_commit_permitted=False fields are not acceptance decisions.
    # Actual successful status, raw phases, gate and preserved acceptance above are required.
    for path, expected in perf['pins_before'].items():
        track(path, expected)
    for path, expected in ((RUNNER, RUNNER_SHA), (HOOK, HOOK_SHA), (SUMMARY, SUMMARY_SHA),
            (PARTIAL, PARTIAL_SHA), (WATCH, WATCH_SHA), (CANONICAL, CANONICAL_SHA)):
        track(path, expected)
    track(Path(__file__))
    summary = load_module(SUMMARY, 'ledger_summary')
    cp_paths = {suffix: DATA / (CP_STEM + suffix) for suffix in CP_HASHES}
    for suffix, expected in CP_HASHES.items():
        track(cp_paths[suffix], expected)
    cp = document(cp_paths['-provenance.json'])
    require(cp['status'] == 'finished' and cp['exit_code'] == 0 and cp['runtime_version'] == '3.14.7'
        and cp['attempted_definitions'] == cp['expected_definitions'] == 97 and not cp['failed_definitions'], 'Historical CP reference differs')
    require(cp['mode'] == 'fast' and cp['benchmarks'] == 'all' and cp['case_timeout_seconds'] == 300
        and cp['case_timeout_overrides'] == {'networkx*': 600}, 'Historical workload policy differs')
    require(Path(cp['runtime_executable']).resolve() == CP.resolve()
        and Path(cp['dependency_site']).resolve() == SITE.resolve(), 'Runtime or dependency site differs')
    require(cp['sha256_start'] == cp['sha256_end'], 'Historical CP binary changed during capture')
    track(CP, cp['sha256_end']['exe'])
    track(CP.with_name('python314.dll'), cp['sha256_end']['dll'])
    require(importlib.metadata.version('pyperformance') == cp['pyperformance_version'] == '1.14.0'
        and importlib.metadata.version('pyperf') == cp['pyperf_version'] == '2.10.0', 'Harness versions differ')
    with CANONICAL.open(encoding='utf-8-sig', newline='') as stream:
        rows = list(csv.DictReader(stream))
    definitions = [r['benchmark'] for r in rows]
    require(len(definitions) == len(set(definitions)) == 97
        and all(re.fullmatch(r'[A-Za-z0-9_]+', n) for n in definitions), 'Canonical enumeration is not exact97')
    expected_subtests = {r['benchmark']: summary.parse_subtests(r['CPython subtests']) for r in rows}
    require(all(r['CPython 3.14 status'] == 'completed' and expected_subtests[r['benchmark']] for r in rows),
        'Canonical CP population is incomplete')
    cp_raw = document(cp_paths['.json'])
    cp_map = summary.benchmark_map(cp_raw)
    require(len(cp_map) == cp['recorded_subtests'] == 124
        and set(summary.case_sections(cp_paths['.log'].read_text(encoding='utf-8'))) == set(definitions), 'Historical population differs')
    require(set(n for names in expected_subtests.values() for n in names) == set(cp_map), 'CP subtest index differs')
    expected_samples = {}
    for name, benchmark in cp_map.items():
        metadata = {**cp_raw.get('metadata', {}), **benchmark.get('metadata', {})}
        values = summary.values(benchmark)
        require(metadata.get('unit') and values and all(math.isfinite(v) and v > 0 for v in values),
            'Historical scored sample population is invalid')
        expected_samples[name] = {'unit': metadata['unit'], 'scored_value_count': len(values)}
    benchmark_python = {p.relative_to(BENCHMARK_ROOT).as_posix(): digest(p) for p in BENCHMARK_ROOT.rglob('*.py')}
    require(benchmark_python == {n.replace('\\', '/'): h for n, h in cp['benchmark_python_sources'].items()}, 'Benchmark Python source differs')
    metadata = {p.relative_to(SITE).as_posix(): digest(p) for p in SITE.glob('*.dist-info/METADATA')}
    require(metadata == {n.replace('\\', '/'): h for n, h in cp['dependency_metadata_sha256'].items()}, 'Dependency metadata differs')
    for p in metadata:
        track(SITE / p, metadata[p])
    input_trees = {}
    for label, directory, bytecode, expected in (
            ('candidate_release', RELEASE, False, release), ('fixed_baseline', BASELINE, False, baseline),
            ('preserved_release', CONTROL / 'Release', False, release), ('preserved_sources', CONTROL / 'sources', False, sources),
            ('benchmark_inputs', BENCHMARK_ROOT, True, None), ('dependency_inputs', SITE, True, None),
            ('manager_pyperf', CP.parent / 'Lib/site-packages/pyperf', True, None),
            ('manager_pyperformance', CP.parent / 'Lib/site-packages/pyperformance', True, None)):
        values = tree_hashes(directory, bytecode)
        require(expected is None or values == expected, 'Complete protected tree differs: ' + label)
        input_trees[label] = {'directory': str(directory.resolve()), 'exclude_bytecode': bytecode, 'sha256': values}
        for name, value in values.items():
            track(contained(directory, name), value)
    # Pin actual current stdlib Python bodies too, without implying historical transitive equality.
    for p in (CP.parent / 'Lib').rglob('*.py'):
        if 'site-packages' not in p.parts and '__pycache__' not in p.parts:
            track(p)
    actual_head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    require(actual_head == args.head, 'Current HEAD differs from accepted CLI identity')
    binding = {'head': args.head, 'receipt_hashes': {k: getattr(args, k + '_sha256') for k in receipts},
        'receipt_paths': {k: str(getattr(args, k).resolve()) for k in receipts},
        'controller_sha256': digest(__file__), 'canonical_sha256': CANONICAL_SHA,
        'source_sha256': sources, 'release_sha256': release, 'baseline_sha256': baseline,
        'definitions': definitions, 'expected_subtests': expected_subtests,
        'expected_subtest_samples': expected_samples, 'protocol': PROTOCOL}
    return binding, pins, input_trees, summary, cp


def stable(pins, input_trees, expected_head):
    observed, mismatches = {}, []
    for path, expected in pins.items():
        try:
            observed[path] = digest(path)
        except (OSError, ValueError) as error:
            observed[path] = None
            mismatches.append({'path': path, 'error': repr(error)})
        if observed[path] != expected:
            mismatches.append({'path': path, 'expected': expected, 'actual': observed[path]})
    tree_changes = {}
    for label, spec in input_trees.items():
        names = tree_names(Path(spec['directory']), spec['exclude_bytecode'])
        expected = set(spec['sha256'])
        if names != expected:
            tree_changes[label] = {'added': sorted(names - expected), 'removed': sorted(expected - names)}
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    return {'passed': not mismatches and not tree_changes and head == expected_head,
        'checked_file_count': len(observed), 'observed_sha256_map_digest': map_digest(observed),
        'expected_sha256_map_digest': map_digest(pins), 'mismatches': mismatches,
        'tree_changes': tree_changes, 'head': head, 'checked_utc': now()}


def authenticate_attempts(ledger):
    for case in ledger['cases']:
        retained = case.get('retained_attempt')
        for entry in case['attempts']:
            require(entry['state'] == 'finished', 'Unfinished attempt: root recovery is required; never auto-kill a stale PID')
            path = contained(DATA, entry['receipt'])
            require(digest(path) == entry['sha256'], 'Attempt receipt changed')
            row = document(path)
            require(row['definition'] == case['definition'] and row['attempt_id'] == entry['attempt_id'], 'Attempt association differs')
            require(row['candidate_binding_digest'] == ledger['binding_digest'], 'Attempt candidate binding differs')
            for name, expected in row['raw_sha256'].items():
                require(digest(contained(DATA, name)) == expected, 'Attempt raw evidence changed')
            if retained == entry['attempt_id']:
                require(row['valid'] and row['definition_status'] in ('completed', 'failed')
                    and row['post_idle']['passed'] and row['post_identity']['passed']
                    and row['measurement_valid'] and row['owned_child_cleanup_completed'], 'Retained case is invalid')
                require(case['status'] == row['definition_status'], 'Retained status differs')
        require(retained is None or sum(e['attempt_id'] == retained for e in case['attempts']) == 1,
            'Missing or duplicate retained attempt')


def attempt_case(args, ledger, case, pins, trees, summary, save):
    number = len(case['attempts']) + 1
    token = args.prefix + '-' + case['definition'] + '-attempt-' + str(number).zfill(2)
    attempt_id = token
    output, log, receipt = (DATA / (token + suffix) for suffix in ('.pyperf.json', '.merged.log', '.attempt.json'))
    require(not any(DATA.glob(token + '*')), 'Attempt prefix already exists')
    row = {'attempt_id': attempt_id, 'definition': case['definition'], 'started_utc': now(),
        'session': ledger['sessions'][-1]['id'], 'state': 'preflight', 'valid': False,
        'definition_status': 'invalid', 'measurement_valid': False,
        'owned_child_cleanup_completed': False, 'raw_sha256': {}, 'idle_guards': [],
        'candidate_binding_digest': ledger['binding_digest'], 'known_orphan_worker_pid': args.known_orphan_worker,
        'known_orphan_worker_creation': args.known_orphan_worker_creation,
        'counter_reset_scope': 'Fresh helper admission only here, untimed before this attempt; never reset during the phase.',
        'runtime_activity_policy': 'Every helper scan also rejects foreign python*/xlang3 processes. Only this manager and observed creation-pinned descendants are admitted; unrelated command lines are not exported.'}
    entry = {'attempt_id': attempt_id, 'state': 'running', 'receipt': receipt.name, 'sha256': None}
    case['attempts'].append(entry)
    save()
    watcher = owned = classify = finish_watch = child = None
    stop = False
    try:
        watcher, owned, admission, classify = make_watcher(row['session'], number,
            args.known_orphan_worker, args.known_orphan_worker_creation)
        row['dormant_worker_admission'] = admission
        entry['manager_identity'] = owned.manager
        entry['dormant_worker_admission'] = admission
        save()

        def idle(label):
            observation = classify(watcher.scan(), manager_only=True)
            check = {'label': label, 'checked_utc': now(), 'passed': not observation['busy'], **observation}
            row['idle_guards'].append(check)
            return check

        row['pre_idle'] = idle('before-attempt')
        require(row['pre_idle']['passed'], 'External activity before child launch')
        row['pre_identity'] = stable(pins, trees, args.head)
        require(row['pre_identity']['passed'], 'Candidate or dependency drift before launch')
        row['launch_idle'] = idle('immediately-before-child')
        require(row['launch_idle']['passed'], 'External activity immediately before launch')
        cap = 600 if case['definition'].startswith('networkx') else 300
        command = [str(CP), str(RUNNER), '--runtime', str(CANDIDATE), '--benchmarks', case['definition'],
            '--mode', 'fast', '--case-timeout', '300', '--case-timeout-override', 'networkx*=600',
            '--dependency-site', str(SITE), '--output', str(output)]
        row.update(command=command, case_cap_seconds=cap, manager_wall_cap_seconds=cap + 120)
        env = os.environ.copy()
        require(not env.get('XLANG3_VM_OPCODE_TIMING'), 'Instrumented official run refused')
        for key in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE', 'PYTHONHOME'):
            env.pop(key, None)
        env.update(XLANG3_PYTHON_LIB=str(CP.parent / 'Lib'), PYTHONPATH=str(HOOK.parent),
            PYTHONIOENCODING='utf-8', PYTHONPYCACHEPREFIX=str(ROOT / 'scratch/performance' / ('pycache-' + args.prefix)))
        finish_watch = watcher.start_timing_process_watch(args.prefix, case['definition'] + '-attempt-' + str(number).zfill(2), row)
        with log.open('xb') as stream:
            child = subprocess.Popen(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                stdout=stream, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
            row.update(pid=child.pid, state='running', child_launched=True)
            entry['pid'] = child.pid
            save()
            owned.seed_child(child, watcher.scan())
            row['child_identity'] = owned.child_identity
            entry['child_identity'] = owned.child_identity
            save()
            row['exit_code'] = child.wait(timeout=cap + 120)
    except BaseException as error:
        row['error'] = repr(error)
        row['interrupted'] = isinstance(error, (KeyboardInterrupt, SystemExit))
        row['manager_wall_timeout'] = isinstance(error, subprocess.TimeoutExpired)
        stop = True
    finally:
        try:
            if child is not None and child.poll() is None:
                # poll(None) still refers to this live Popen-owned process handle;
                # its PID cannot be a recycled foreign process. Never target a dead PID.
                row['cleanup_ownership_proof'] = 'Live process handle returned by this attempt Popen; descendants only via its live PID.'
                try:
                    subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)],
                        capture_output=True, timeout=10, check=False)
                finally:
                    if child.poll() is None:
                        child.kill()
                    child.wait(timeout=10)
            row['owned_child_cleanup_completed'] = child is None or child.poll() is not None
        except BaseException as error:
            row['cleanup_error'] = repr(error)
            row['owned_child_cleanup_completed'] = False
            stop = True
        finally:
            if finish_watch is not None:
                try:
                    row['measurement_valid'] = finish_watch()
                except BaseException as error:
                    row.update(measurement_valid=False, watch_finish_error=repr(error))
                    stop = True
            if owned is not None:
                row['observed_owned_process_identities'] = owned.snapshot()
            try:
                if watcher is not None:
                    observation = classify(watcher.scan(), manager_only=True)
                    row['post_idle'] = {'passed': not observation['busy'], 'checked_utc': now(), **observation}
                else:
                    row['post_idle'] = {'passed': False, 'reason': 'Activity admission failed'}
                row['post_identity'] = stable(pins, trees, args.head)
            except BaseException as error:
                row['post_guard_error'] = repr(error)
                row.setdefault('post_idle', {'passed': False})
                row.setdefault('post_identity', {'passed': False})
                stop = True
            try:
                watch_path = DATA / (token + '.external-process-observations.jsonl')
                for path in (log, output, watch_path):
                    if path.is_file():
                        row['raw_sha256'][path.name] = digest(path)
                watch_meta = row.get('external_process_watch')
                if watch_meta is not None:
                    row['raw_sha256'][watch_meta['log']] = watch_meta['sha256']
                partial_dir = output.parent / (output.stem + '-partial')
                if partial_dir.is_dir():
                    for path in sorted(partial_dir.rglob('*')):
                        if path.is_file():
                            row['raw_sha256'][path.relative_to(DATA).as_posix()] = digest(path)
            except BaseException as error:
                row['raw_hash_error'] = repr(error)
                stop = True
        try:
            timing_safe = (row.get('child_launched', False) and 'error' not in row and 'raw_hash_error' not in row
                and row['measurement_valid'] and row['owned_child_cleanup_completed']
                and row['post_idle']['passed'] and row['post_identity']['passed'])
            if timing_safe:
                text = log.read_text(encoding='utf-8', errors='replace')
                sections = summary.case_sections(text)
                headers = [summary.CASE_LINE.match(line).group(1) for line in text.splitlines() if summary.CASE_LINE.match(line)]
                failures, details = summary.failure_details(text)
                require(headers == [case['definition']] and set(sections) == {case['definition']}, 'Unexpected selected definition')
                if row['exit_code'] == 0:
                    require(not failures and output.is_file(), 'Successful manager has missing/failed output')
                    raw = document(output)
                    benchmarks = summary.benchmark_map(raw)
                    require(set(benchmarks) == set(ledger['binding']['expected_subtests'][case['definition']]), 'Subtest population differs')
                    samples = {}
                    for name, benchmark in benchmarks.items():
                        metadata = {**raw.get('metadata', {}), **benchmark.get('metadata', {})}
                        values = summary.values(benchmark)
                        expected = ledger['binding']['expected_subtest_samples'][name]
                        require(metadata.get('unit') == expected['unit'] and len(values) == expected['scored_value_count']
                            and all(math.isfinite(v) and v > 0 for v in values),
                            'Successful scored sample count/unit/values differ: ' + name)
                        samples[name] = {'unit': metadata['unit'], 'scored_value_count': len(values)}
                    row.update(valid=True, definition_status='completed', recorded_subtests=list(benchmarks),
                        verified_sample_population=samples)
                else:
                    require(set(failures) == {case['definition']}, 'Manager failure lacks original definition failure evidence')
                    row.update(valid=True, definition_status='failed', failure=failures[case['definition']],
                        failure_detail=details.get(case['definition']), partial_timings_never_scored=True)
        except BaseException as error:
            row.update(valid=False, definition_status='invalid', classification_error=repr(error))
        row.update(state='finished', finished_utc=now())
        row['timing_scoring_permitted'] = row['valid'] and row['definition_status'] == 'completed'
        write_attempt(receipt, row)
        entry.update(state='finished', sha256=digest(receipt), valid=row['valid'], child_launched=row.get('child_launched', False))
        if row['valid']:
            case.update(status=row['definition_status'], retained_attempt=attempt_id)
        else:
            case['status'] = ('invalid_exhausted' if sum(bool(a.get('child_launched')) for a in case['attempts'])
                >= MAX_CHILD_ATTEMPTS else 'invalid_pending')
        save()
    # Normal completed/failure rows never stop enumeration. Invalid activity
    # may stop this session; the complete97 ledger survives for explicit resume.
    stop |= not row['post_idle']['passed'] or not row['post_identity']['passed'] or not row['owned_child_cleanup_completed']
    return stop, row


def main():
    require(not sys.flags.optimize, 'Run the controller without -O/PYTHONOPTIMIZE')
    require(Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7), 'Use unoptimized fixed CPython3.14.7')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--head', required=True)
    parser.add_argument('--prefix', required=True)
    for name in ('application', 'build', 'correctness', 'performance', 'accepted-manifest'):
        parser.add_argument('--' + name, type=Path, required=True)
        parser.add_argument('--' + name + '-sha256', required=True)
    parser.add_argument('--resume-sha256', help='Exact last owned ledger hash; omission creates a fresh ledger')
    parser.add_argument('--known-orphan-worker', type=int, help='One explicit known orphan PID; no discovery or wildcard admission')
    parser.add_argument('--known-orphan-worker-creation', help='Exact CreationDate string for the explicitly authorized orphan PID')
    parser.add_argument('--authenticate-only', action='store_true', help='Read and check all lineage/input pins; print a result without creating a ledger or child')
    args = parser.parse_args()
    require(re.fullmatch(r'[0-9a-f]{40}', args.head) and re.fullmatch(r'[a-z0-9-]+', args.prefix), 'Invalid exact HEAD/prefix')
    require(args.known_orphan_worker is None or args.known_orphan_worker > 0, 'Invalid explicit worker PID')
    require((args.known_orphan_worker is None) == (args.known_orphan_worker_creation is None),
        'Supply both the known orphan PID and its exact CreationDate, or neither')
    binding, pins, trees, summary, cp = authenticate(args)
    if args.authenticate_only:
        check = stable(pins, trees, args.head)
        require(check['passed'], 'Authentication-only terminal input drift')
        print(json.dumps({'status': 'authentication_passed_no_children', 'head': args.head,
            'binding_digest': map_digest(binding), 'controller_sha256': digest(__file__),
            'source_count': len(binding['source_sha256']), 'release_count': len(binding['release_sha256']),
            'baseline_count': len(binding['baseline_sha256']), 'definition_count': len(binding['definitions']),
            'expected_subtest_samples': binding['expected_subtest_samples'],
            'checked_input_file_count': len(pins), 'identity_check': check,
            'scope': 'No ledger created, activity admission used or benchmark launched.'}, indent=2), flush=True)
        return 0
    path = DATA / (args.prefix + '-ledger.json')
    if args.resume_sha256:
        require(re.fullmatch(r'[0-9a-f]{64}', args.resume_sha256) and digest(path) == args.resume_sha256, 'Resume ledger hash mismatch')
        ledger = document(path)
        require(ledger['protocol'] == PROTOCOL and ledger['binding'] == binding and ledger['pins'] == pins
            and ledger['input_trees'] == trees and ledger['max_child_attempts_per_definition'] == MAX_CHILD_ATTEMPTS,
            'Resume candidate, inputs or policy changed')
        require([c['definition'] for c in ledger['cases']] == binding['definitions'], 'Resume list differs')
        authenticate_attempts(ledger)
        require(not ledger['capture_complete'], 'All definitions already finished; never repeat them')
    else:
        require(not any(DATA.glob(args.prefix + '*')), 'Fresh prefix required; previous evidence must survive')
        ledger = {'protocol': PROTOCOL, 'created_utc': now(), 'binding': binding, 'binding_digest': map_digest(binding),
            'pins': pins, 'input_trees': trees, 'max_child_attempts_per_definition': MAX_CHILD_ATTEMPTS,
            'expected_definitions': 97, 'capture_complete': False, 'suite_passed': False, 'sessions': [],
            'cases': [{'definition': name, 'status': 'pending', 'retained_attempt': None, 'attempts': []} for name in binding['definitions']],
            'historical_cpython': {'prefix': CP_STEM, 'sha256': CP_HASHES, 'version': '3.14.7',
                'started_utc': cp['started_utc'], 'completed_utc': cp['completed_utc'], 'comparison': 'Historical October7, unpaired; no CP full rerun'},
            'protocol_change': 'Each original definition gets a fresh manager process and independent activity window; identical original source/fast settings/case caps. This is not a single-process all-97 attempt.',
            'identity_scope': '143 selected recorded source files, complete Release178 and fixed baseline177, preserved143/178 and current dependency/stdlib bytes; not a clean-checkout or historical transitive-byte proof.',
            'sampling_limit': 'One-second OS polls can miss short foreign processes. Unseen children whose parent exited cannot be proven owned and invalidate; observed descendant creation identities remain pinned.',
            'retry_policy': 'No within-session retry; at most three owned child launches per definition across explicit resumes. Valid benchmark failures are final. Preflight refusals preserve evidence without consuming a child launch.'}
    session = {'id': str(len(ledger['sessions']) + 1).zfill(3), 'started_utc': now(), 'manager_pid': os.getpid(),
        'known_orphan_worker_pid': args.known_orphan_worker, 'previous_ledger_sha256': args.resume_sha256}
    ledger['sessions'].append(session)
    ledger.update(status='running', terminal=False, resumable=False)
    save = lambda: atomic_save(path, ledger)
    save()
    try:
        for case in ledger['cases']:
            if case['retained_attempt'] is not None:
                continue
            child_attempts = sum(bool(a.get('child_launched')) for a in case['attempts'])
            if child_attempts >= MAX_CHILD_ATTEMPTS:
                case['status'] = 'invalid_exhausted'
                save()
                continue
            print('Starting original definition', case['definition'], 'completed ledger rows',
                sum(c['retained_attempt'] is not None for c in ledger['cases']), 'of 97', flush=True)
            stop, row = attempt_case(args, ledger, case, pins, trees, summary, save)
            print('Definition', case['definition'], row['definition_status'], 'valid', row['valid'], flush=True)
            if stop:
                ledger['stop_reason'] = row.get('error') or row.get('post_guard_error') or 'Post-attempt activity/identity/cleanup invalid'
                break
    except BaseException as error:
        ledger['controller_error'] = repr(error)
    finally:
        retained = [c for c in ledger['cases'] if c['retained_attempt'] is not None]
        pending = [c['definition'] for c in ledger['cases'] if c['retained_attempt'] is None and c['status'] != 'invalid_exhausted']
        exhausted = [c['definition'] for c in ledger['cases'] if c['status'] == 'invalid_exhausted']
        ledger['terminal_identity'] = stable(pins, trees, args.head)
        ledger.update(terminal=True, capture_complete=len(retained) == 97 and ledger['terminal_identity']['passed'],
            completed_definitions=sum(c['status'] == 'completed' for c in retained),
            failed_definitions=[c['definition'] for c in retained if c['status'] == 'failed'],
            pending_definitions=pending, exhausted_invalid_definitions=exhausted,
            valid_finished_definitions=len(retained), resumable=bool(pending) or not ledger['terminal_identity']['passed'], finished_utc=now())
        ledger['suite_passed'] = ledger['capture_complete'] and not ledger['failed_definitions']
        ledger['status'] = ('complete_all97_passed' if ledger['suite_passed'] else
            'complete_all97_with_benchmark_failures' if ledger['capture_complete'] else
            'resumable_candidate_or_input_drift' if not ledger['terminal_identity']['passed'] else
            'resumable_pending_invalid_or_unstarted' if pending else 'terminal_incomplete_invalid_attempts_exhausted')
        session.update(finished_utc=now(), status=ledger['status'])
        save()
    print(json.dumps({'ledger': str(path), 'sha256': digest(path), 'status': ledger['status'],
        'valid_finished_definitions': ledger['valid_finished_definitions'], 'completed_definitions': ledger['completed_definitions'],
        'failed_definitions': ledger['failed_definitions'], 'pending_definitions': ledger['pending_definitions'],
        'capture_complete': ledger['capture_complete'], 'suite_passed': ledger['suite_passed']}, indent=2), flush=True)
    return 0 if ledger['capture_complete'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
