# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0.
"""Root-only unchanged raytrace counter evidence and balanced body prefilter.

Counters are aggregate entry-source dispatch evidence, never timing scores or
exact helper-hit counts. A useful paired signal only requests full validation.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import random
import re
import statistics
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BASELINE = ROOT / 'build-repro/Release'
SITE = ROOT / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'
SOURCE = CP.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks/bm_raytrace/run_benchmark.py'
BASE = ROOT / 'scratch/performance/run-gc-r7b-all97-per-definition-ledger-proposed-20261009.py'
OWNERSHIP = ROOT / 'scratch/performance/run-gc-r7b-all97-ownership-supplement-r3-proposed-20261009.py'
FIXTURE = ROOT / 'tests/fixtures/core/two_argument_double_ir_plan.py'
EXPECTED = ROOT / 'tests/fixtures/expected/two_argument_double_ir_plan.out'
FROZEN = {
    str(BASE): '48fafce1a804f1970c63e20ce47c66f593686d2df98f2c678ea8a56ec987e4b6',
    str(OWNERSHIP): '517113f30f85dc73c62434865e564e2bca6a21331ef988dc438f10ceb4416f53',
    str(ROOT / 'scratch/performance/gc-phase-dormant-msbuild-activity-watch-20261009.py'): '6dea8abf4f66ff0d2a95830e4c560202359c57502f0f635ecf9e1dda5494bd48',
    str(SOURCE): '88ef4d9060d8e8f6ce40f376477aaf89cc808fa44813225a3071a05a1467f017',
    str(FIXTURE): '406434854572605d6e699d54bcd2c1b9b524b1f403aeec70f7190c5f57b514f4',
    str(EXPECTED): '154e54becd1e12cb2d4a8461bc8739f786a9a9136a1cbec8be8bf40ad6adaeac',
}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    require(Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7)
            and sys.flags.isolated and not sys.flags.optimize and sys.gettrace() is None
            and sys.getprofile() is None, 'Use unoptimized isolated CPython 3.14.7 without observers')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('counters', 'paired', 'official'))
    for label in ('application', 'build', 'focus'):
        parser.add_argument('--' + label, type=Path, required=True)
        parser.add_argument('--' + label + '-sha256', required=True)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    require(re.fullmatch(r'two-argument-ir-raytrace-(?:counters|paired|official)-r(?:[3-9]|[1-9][0-9]+)-20261010', args.prefix)
            and args.prefix.split('-')[4] == args.mode, 'Use one fresh mode-specific prefix')
    require(not any(DATA.glob(args.prefix + '*')), 'Prefix exists; retain evidence and review, no retry')
    pins = dict(FROZEN)
    def track(path, expected):
        key = str(path)
        require(key not in pins or pins[key] == expected, 'Authenticated pin overwritten: ' + key)
        pins[key] = expected
    pins[str(Path(__file__).resolve())] = sha(__file__)
    for label in ('application', 'build', 'focus'):
        path = getattr(args, label).resolve(strict=True)
        expected = getattr(args, label + '_sha256')
        require(sha(path) == expected, label + ' receipt hash differs')
        pins[str(path)] = expected
    require(all(sha(p) == h for p, h in pins.items()), 'Frozen proposal/input/helper changed')
    base = load(BASE, 'raytrace_base')
    base.OwnedProcesses = load(OWNERSHIP, 'raytrace_ownership').ownership_class(base)
    app, build, focus = (read(getattr(args, n)) for n in ('application', 'build', 'focus'))
    require(app['terminal'] and app['passed'] and app['status'] == 'applied', 'Application incomplete')
    manifest = Path(app['accepted_control_manifest_path']).resolve(strict=True)
    require(sha(manifest) == app['accepted_control_manifest_sha256']
            == 'aa2d9f715ad11e78ffa58024c10631c37e7d83d816c4a8cb0523f6469287547f', 'Accepted control differs')
    pins[str(manifest)] = sha(manifest)
    proposal_path = Path(app['proposal_path']).resolve(strict=True)
    require(proposal_path.is_relative_to((ROOT / 'scratch/performance').resolve())
            and sha(proposal_path) == app['proposal_sha256'], 'Application proposal identity differs')
    track(proposal_path, app['proposal_sha256'])
    control, proposal = read(manifest), read(proposal_path)
    trial_patch = Path(app['trial_patch_path']).resolve(strict=True)
    require(trial_patch == (ROOT / proposal['patch']).resolve(strict=True)
            and sha(trial_patch) == app['trial_patch_sha256'] == proposal['patch_sha256'], 'Full merged trial patch differs')
    track(trial_patch, app['trial_patch_sha256'])
    require(control['terminal'] and control['full_validated'] and control['fixed_gate_passed'], 'Control unvalidated')
    expected_before = {**control['source_sha256'], **app['harness_overlay_sha256']}
    require(app['before_source_sha256'] == expected_before
            and app['harness_overlay_sha256'] == {'tests/run_fixtures.ps1': '30d462945ab4f8b546269b4921df20688efa033cf16eb8a85a1e9c4f3297050e'}, 'Pretrial layer differs')
    engine = set(proposal['owned_engine_paths'])
    require(engine and engine == set(proposal['candidate_sha256'])
            and engine <= set(control['source_sha256'])
            and all(p.startswith('src/') for p in engine), 'Merged numeric engine scope differs')
    require(proposal['input_sha256'] == {p: expected_before[p] for p in engine}, 'Merged accepted input map differs')
    for p, h in proposal['candidate_sha256'].items():
        candidate = ROOT / proposal['candidate_root'] / p
        require(sha(candidate) == h, 'Merged candidate bytes changed')
        track(candidate, h)
    changed = engine | {'tests/run_fixtures.py', 'tests/run_fixtures.ps1'}
    added = {'tests/fixtures/core/two_argument_double_ir_plan.py', 'tests/fixtures/expected/two_argument_double_ir_plan.out'}
    require(set(app['allowed_changed_source_paths']) == changed and set(app['added_source_paths']) == added,
            'Application scope differs')
    sources = app['source_sha256']
    require(set(sources) == set(expected_before) | added and app['source_count'] == len(sources) == 145
            and all(sources[p] == h for p, h in proposal['candidate_sha256'].items())
            and all(sources[p] == h for p, h in expected_before.items() if p not in changed), 'Candidate source map differs')
    require(build['terminal'] and build['passed'] and build['status'] == 'build_passed'
            and build['exit_code'] == 0 and build['owned_child_cleanup_completed'] and build['sources_unchanged']
            and build['application_sha256'] == args.application_sha256 and build['source_sha256'] == sources,
            'Fresh candidate build incomplete or different')
    release = build['release_sha256']
    require(len(release) == 178 and release != control['release_sha256'], 'Candidate Release missing/unchanged')
    semantic_pins = {str(ROOT / p): h for p, h in sources.items()}
    semantic_pins.update({str(RELEASE / p): h for p, h in release.items()})
    for mapping in (app['protected_test_input_sha256'], app['unowned_tracked_dirty_sha256']):
        semantic_pins.update({str(ROOT / p): h for p, h in mapping.items()})
    semantic_pins.update({str(BASELINE / p): h for p, h in app['fixed_baseline_sha256'].items()})
    require(focus['terminal'] and focus['passed'] and focus['exit_code'] == 0 and not focus['scored']
            and focus['application_sha256'] == args.application_sha256 and focus['build_sha256'] == args.build_sha256
            and focus['inputs_unchanged'] and focus['pins'] == semantic_pins
            and [Path(p).resolve() for p in focus['command']] == [(RELEASE / 'xlang3.exe').resolve(), FIXTURE.resolve()],
            'Actual candidate strict8 prerequisite differs')
    for name in ('stdout', 'stderr'):
        path = DATA / focus[name]
        require(sha(path) == focus[name + '_sha256'], 'Candidate raw stream changed')
        pins[str(path)] = sha(path)
    normal = lambda b: b.replace(b'\r\r\n', b'\n').replace(b'\r\n', b'\n')
    require(normal((DATA / focus['stdout']).read_bytes()) == normal(EXPECTED.read_bytes())
            and not (DATA / focus['stderr']).read_bytes(), 'Candidate strict8 parity absent')
    reference_path = Path(app['reference_path'])
    require(sha(reference_path) == app['reference_sha256']
            == '982014bca87d3712a170c0fcc455701555746864cb614ecd08afd27dd092fe58', 'CP/accepted semantic reference differs')
    reference = read(reference_path)
    require(reference['terminal'] and reference['status'] == 'semantic_reference_passed'
            and reference['inputs_unchanged'] and len(reference['phases']) == 2
            and all(r['passed'] and r['exit_code'] == 0 for r in reference['phases']), 'CP/accepted reference incomplete')
    pins[str(reference_path)] = sha(reference_path)
    for path in (CP, CP.with_name('python314.dll')):
        require(sha(path) == reference['pins'][str(path)], 'CPython reference binary changed')
    for row in reference['phases']:
        for name in ('stdout', 'stderr'):
            path = DATA / (reference_path.stem + '-' + row['name'] + '.' + name + '.log')
            require(sha(path) == row[name + '_sha256'], 'CP/accepted raw reference changed')
            pins[str(path)] = sha(path)
    reference_controller = ROOT / 'scratch/performance/check-two-argument-ir-fixture-r3-reference-20261010.py'
    candidate_controller = Path(focus['controller_path']).resolve(strict=True)
    require(candidate_controller.is_relative_to((ROOT / 'scratch/performance').resolve()), 'Semantic producer path outside scratch')
    require(sha(reference_controller) == reference['controller_sha256']
            and sha(candidate_controller) == focus['controller_sha256'], 'Semantic producer changed')
    pins[str(reference_controller)] = sha(reference_controller)
    pins[str(candidate_controller)] = sha(candidate_controller)
    require(app['fixed_baseline_sha256'] == control['fixed_baseline_sha256'], 'Gate baseline changed')
    control_root = manifest.parent
    trees = {}
    for label, directory, bytecode, expected in (
        ('candidate_release', RELEASE, False, release), ('baseline', BASELINE, False, control['fixed_baseline_sha256']),
        ('control_release', control_root / 'Release', False, control['release_sha256']),
        ('control_sources', control_root / 'sources', False, control['source_sha256']),
        ('dependency_site', SITE, True, None), ('manager_pyperf', CP.parent / 'Lib/site-packages/pyperf', True, None),
        ('manager_pyperformance', CP.parent / 'Lib/site-packages/pyperformance', True, None)):
        values = base.tree_hashes(directory, bytecode)
        require(expected is None or values == expected, 'Complete tree mismatch: ' + label)
        trees[label] = {'directory': str(directory), 'exclude_bytecode': bytecode, 'sha256': values}
        for p, h in values.items(): track(directory / p, h)
    for mapping in (sources, app['protected_test_input_sha256'], app['unowned_tracked_dirty_sha256']):
        for p, h in mapping.items(): track(ROOT / p, h)
    for path in (CP, CP.with_name('python314.dll'), base.RUNNER, base.HOOK, base.PARTIAL, ROOT / 'src/xlang3.cpp',
                 ROOT / 'src/runtime/perf_counters.cpp', ROOT / 'src/internal/xlang3/perf_counters.h'):
        track(path, sha(path))
    require(pins[str(base.RUNNER)] == base.RUNNER_SHA and pins[str(base.HOOK)] == base.HOOK_SHA
            and pins[str(base.PARTIAL)] == base.PARTIAL_SHA, 'Original runner/hook changed')
    build_log = DATA / build['log']
    require(sha(build_log) == build['log_sha256'], 'Build log changed')
    pins[str(build_log)] = sha(build_log)
    head = app['head']
    record = dict(terminal=False, status='preflight', mode=args.mode, phases=[], head=head,
                  application_sha256=args.application_sha256, build_sha256=args.build_sha256,
                  focus_sha256=args.focus_sha256, source_sha256=sources, release_sha256=release,
                  proposal_path=str(proposal_path), proposal_sha256=app['proposal_sha256'],
                  semantic_controller_path=str(candidate_controller), semantic_controller_sha256=focus['controller_sha256'],
                  engine_path_count=len(engine),
                  baseline_sha256=control['fixed_baseline_sha256'], pins=pins, input_trees=trees,
                  original_source_sha256=FROZEN[str(SOURCE)], dimensions=[100, 100], no_auto_retry=True,
                  timing_scoring_permitted=False, full_checks_required_before_acceptance=True,
                  scope='Unchanged original body; counter mode aggregate only; paired mode unscored prefilter; official mode fast unpaired control/candidate')
    receipt = DATA / (args.prefix + '.json')
    def save():
        base.atomic_save(receipt, record)
    env = os.environ.copy()
    for key in tuple(env):
        if key.startswith(('PYTHON', 'XLANG3_')) or key in ('_NT_SYMBOL_PATH', '_NT_ALT_SYMBOL_PATH'):
            env.pop(key, None)
    env.update(PYTHONPATH=os.pathsep.join((str(base.HOOK.parent), str(SITE))),
               XLANG3_PYTHON_LIB=str(CP.parent / 'Lib'), PYTHONIOENCODING='utf-8',
               PYTHONUNBUFFERED='1', PYTHONDONTWRITEBYTECODE='1')
    record['child_environment'] = {k: env[k] for k in env if k.startswith(('PYTHON', 'XLANG3_'))}
    def phase(name, command, output, counter_mode=False):
        row = dict(name=name, command=list(map(str, command)), valid=False, counter_mode=counter_mode)
        record['phases'].append(row); save()
        watcher, owned, admission, classify = base.make_watcher(args.prefix, name, None, None)
        row['admission'] = admission
        child = finish = None
        stdout, stderr = (DATA / (args.prefix + '-' + name + '.' + s + '.log') for s in ('stdout', 'stderr'))
        def idle():
            result = classify(watcher.scan(), manager_only=True)
            return {'passed': not result['busy'], **result}
        def cleanup_descendants():
            if child is None or not getattr(owned, 'child_identity', None):
                return
            identities = owned.snapshot()
            all_known = {(p['ProcessId'], p['CreationDate']): p for p in identities}
            root_key = (owned.child_identity['ProcessId'], owned.child_identity['CreationDate'])
            known = {root_key: owned.child_identity}
            while True:
                changed = False
                for key, identity in all_known.items():
                    if key in known or identity['ProcessId'] == owned.manager['ProcessId']:
                        continue
                    child_created = base.creation_seconds(identity['CreationDate'])
                    parents = [p for p in identities if p['ProcessId'] == identity['ParentProcessId']
                               and base.creation_seconds(p['CreationDate']) <= child_created]
                    if not parents: continue
                    # Use the newest observed compatible parent lifetime. Older
                    # rows never become descendants of a newly reused parent PID,
                    # and the manager is never a cleanup target.
                    parent = max(parents, key=lambda p: base.creation_seconds(p['CreationDate']))
                    if (parent['ProcessId'], parent['CreationDate']) in known:
                        known[key] = identity
                        changed = True
                if not changed: break
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
            kernel.OpenProcess.restype = wintypes.HANDLE
            kernel.TerminateProcess.argtypes = (wintypes.HANDLE, wintypes.UINT)
            kernel.TerminateProcess.restype = wintypes.BOOL
            kernel.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
            kernel.WaitForSingleObject.restype = wintypes.DWORD
            kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
            kernel.CloseHandle.restype = wintypes.BOOL
            terminated = []
            for current in watcher.scan():
                key = (current['ProcessId'], current.get('CreationDate'))
                if key not in known: continue
                require(base.process_identity(current) == known[key], 'Owned descendant identity changed')
                handle = kernel.OpenProcess(0x100001, False, current['ProcessId'])
                if not handle:
                    require(not any((p['ProcessId'], p.get('CreationDate')) == key for p in watcher.scan()),
                            'Cannot open still-live owned descendant')
                    continue
                try:
                    # Hold the actual process handle, then revalidate its observed
                    # creation-pinned identity. Termination uses HANDLE, never a
                    # dead manager PID or an unowned/foreign process PID.
                    again = next((p for p in watcher.scan() if p['ProcessId'] == key[0]), None)
                    require(again is None or base.process_identity(again) == known[key], 'Descendant PID reused')
                    if again is not None:
                        require(kernel.TerminateProcess(handle, 1), 'Owned descendant termination failed')
                        require(kernel.WaitForSingleObject(handle, 15000) == 0, 'Owned descendant did not exit')
                        terminated.append(known[key])
                finally:
                    kernel.CloseHandle(handle)
            row['terminated_owned_descendant_identities'] = terminated
            require(not any((p['ProcessId'], p.get('CreationDate')) in known for p in watcher.scan()),
                    'Owned descendants remain after cleanup')
        try:
            row['pre_idle'] = idle(); require(counter_mode or row['pre_idle']['passed'], 'External activity before ' + name)
            row['pre_identity'] = base.stable(pins, trees, head); require(row['pre_identity']['passed'], 'Changed inputs before ' + name)
            row['launch_idle'] = idle(); require(counter_mode or row['launch_idle']['passed'], 'External activity at launch ' + name)
            finish = watcher.start_timing_process_watch(args.prefix, name, row)
            with stdout.open('xb') as out, stderr.open('xb') as err:
                child = subprocess.Popen(row['command'], cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                                         stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
                owned.seed_child(child, watcher.scan()); row['child_identity'] = owned.child_identity
                row['exit_code'] = child.wait(timeout=420 if args.mode == 'official' else 300)
            require(row['exit_code'] == 0, 'Original raytrace worker failed')
            document = read(output); benches = document['benchmarks']
            require(len(benches) == 1, 'Wrong original benchmark count')
            bench = benches[0]; metadata = {**document.get('metadata', {}), **bench.get('metadata', {})}
            require(metadata.get('name') == 'raytrace' and metadata.get('raytrace_width') == 100
                    and metadata.get('raytrace_height') == 100 and metadata.get('unit') == 'second', 'Original population/dimensions/unit differ')
            values = [v for run in bench['runs'] for v in run.get('values', [])]
            require(len(values) == (20 if args.mode == 'official' else 1)
                    and all(math.isfinite(v) and v > 0 for v in values), 'Wrong scored value population')
            if args.mode != 'official':
                require(len(bench['runs']) == 1 and not bench['runs'][0].get('warmups', [])
                        and {**metadata, **bench['runs'][0].get('metadata', {})}.get('loops') == 1,
                        'Body worker loops/warmups differ')
            row.update(values=values, mean_seconds=statistics.mean(values))
            stderr_text = stderr.read_text(encoding='utf-8-sig')
            if counter_mode:
                require('perf: native_calls=' in stderr_text and 'perf: objects kind alloc' in stderr_text,
                        'Existing counter report absent')
                require(not any(not line.startswith('perf: ') for line in stderr_text.splitlines()), 'Unexpected counter stderr')
                require(not any(int(n) for n in re.findall(r'perf: monitoring .* callbacks=(\d+)', stderr_text)), 'Observer callbacks active')
                row['opcode_dispatches'] = {int(i): int(n) for i, n in re.findall(r'^perf: opcode (\d+) (\d+)$', stderr_text, re.M)}
            else:
                require(not stderr.read_bytes(), 'Unexpected original stderr')
        except BaseException as error:
            row['error'] = repr(error)
        finally:
            try:
                if child is not None and child.poll() is None:
                    # CPython's Popen.kill uses its held Windows process handle;
                    # a dying/recycled PID cannot become a foreign cleanup target.
                    child.kill()
                    child.wait(timeout=15)
                cleanup_descendants()
                row['cleanup_passed'] = child is None or child.poll() is not None
            except BaseException as error:
                row['cleanup_error'] = repr(error)
            for action in ('watch', 'idle', 'identity'):
                try:
                    if action == 'watch': row['measurement_valid'] = finish() if finish else False
                    elif action == 'idle': row['post_idle'] = idle()
                    else: row['post_identity'] = base.stable(pins, trees, head)
                except BaseException as error:
                    row[action + '_guard_error'] = repr(error)
            row['raw_sha256'] = {str(p): sha(p) for p in (stdout, stderr, output) if p.is_file()}
            row['observed_owned_process_identities'] = owned.snapshot()
            semantic_valid = (not any(k.endswith('error') for k in row) and row.get('exit_code') == 0
                              and row.get('cleanup_passed', False) and row.get('post_identity', {}).get('passed', False))
            if counter_mode:
                semantic_valid = (semantic_valid and bool(row.get('external_process_watch'))
                                  and not row['external_process_watch'].get('scanner_errors'))
            strict_activity_valid = (row.get('measurement_valid', False)
                                     and row.get('post_idle', {}).get('passed', False))
            row['counts_valid'] = semantic_valid if counter_mode else False
            row['valid'] = semantic_valid and (counter_mode or strict_activity_valid)
            row['timing_used_for_prefilter'] = row['valid'] and not counter_mode
            row['activity_policy'] = ('Unscored process-local counters: retain all strict activity flags/overlaps; foreign CPU activity is not a counter-semantic failure'
                                      if counter_mode else 'Strict timing idle/watch unchanged; all activity invalidates timing')
            save()
        require(row['valid'], 'Invalid attempt retained; stop without retry: ' + name)
        print(name + ' PASS', flush=True)
        return row
    def run(label, name, counters=False):
        exe = (control_root / 'Release' if label == 'control' else RELEASE) / 'xlang3.exe'
        result = DATA / (args.prefix + '-' + name + '.pyperf.json')
        if args.mode == 'official':
            command = [CP, base.RUNNER, '--runtime', exe, '--benchmarks', 'raytrace', '--mode', 'fast',
                       '--case-timeout', '300', '--dependency-site', SITE, '--output', result]
        else:
            command = [exe] + (['--perf-counters'] if counters else []) + [SOURCE, '--worker', '--worker-task', '0',
                       '--loops', '1', '--values', '1', '--warmups', '0', '--width', '100', '--height', '100', '--output', result]
        return phase(name, command, result, counters)
    try:
        save()
        if args.mode == 'counters':
            old, new = run('control', 'control', True), run('candidate', 'candidate', True)
            enum = (ROOT / 'src/internal/xlang3/ir.h').read_text(encoding='utf-8').split('enum class Op : uint16_t {', 1)[1].split('};', 1)[0]
            enum = re.sub(r'/\*.*?\*/|//[^\n]*', '', enum, flags=re.S)
            names = [token.strip() for token in enum.split(',') if token.strip()]
            require(all(re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', n) for n in names), 'Explicit opcode enum values require review')
            deltas = {n: old['opcode_dispatches'].get(i, 0) - new['opcode_dispatches'].get(i, 0) for i, n in enumerate(names)}
            family = {n: deltas[n] for n in ('LoadLocalAttr', 'Mul', 'Add', 'CallLocalMethod', 'Pop', 'Return')}
            n = family['LoadLocalAttr'] // 6
            supported = (n > 0 and family['LoadLocalAttr'] == 6*n and family['Mul'] == 3*n
                         and family['Add'] == 2*n and family['CallLocalMethod'] == n
                         and family['Pop'] == n and family['Return'] == n)
            record['counter_evidence'] = dict(opcode_reduction_control_minus_candidate=deltas, dot_family_reductions=family,
                exact_dot_dispatch_pattern_supported=supported,
                inferred_equivalent_removed_body_dispatches=n if supported else None,
                limitation='Aggregate entry imports/setup/body. Pattern supports plan admission; not an exact helper hit count, eligible coverage fraction, per-function attribution or CPU share.')
        elif args.mode == 'paired':
            pairs = []
            for i in range(7):
                order = ('control', 'candidate') if i % 2 == 0 else ('candidate', 'control')
                rows = {label: run(label, 'pair-' + str(i + 1) + '-' + label) for label in order}
                pairs.append(rows['control']['mean_seconds'] / rows['candidate']['mean_seconds'])
            rng = random.Random(20261010)
            draws = sorted(statistics.median(pairs[rng.randrange(7)] for _ in range(7)) for _ in range(50000))
            def percentile(p):
                index = p * (len(draws)-1); lo, hi = math.floor(index), math.ceil(index)
                return draws[lo] + (draws[hi]-draws[lo]) * (index-lo)
            median, ci = statistics.median(pairs), [percentile(.025), percentile(.975)]
            record['paired_summary'] = dict(pair_ratios_control_over_candidate=pairs, median_ratio=median, bootstrap95_ci=ci,
                resamples=50000, seed=20261010, useful_prefilter_signal=median > 1.02 and ci[0] > 1,
                rule='Seven alternating pairs; median >1.02 and lower95 >1 requests full checks only; no automatic acceptance',
                limit='One unchanged original body per fresh worker, no warmups; unscored diagnostic, not official fast20 or CPython comparison')
        else:
            old, new = run('control', 'control'), run('candidate', 'candidate')
            record['official_summary'] = dict(control_mean_seconds=old['mean_seconds'], candidate_mean_seconds=new['mean_seconds'],
                ratio_control_over_candidate=old['mean_seconds']/new['mean_seconds'], values_per_runtime=20,
                limit='Sequential fresh original fast20; unpaired control/candidate, no CP comparison or standalone acceptance')
        record['status'] = 'diagnostic_completed'
    except BaseException as error:
        record.update(status='diagnostic_failed_or_invalid', error=repr(error))
    finally:
        record['terminal_identity'] = base.stable(pins, trees, head)
        record['hashes_unchanged'] = record['terminal_identity']['passed']
        record['terminal'] = True
        if not record['hashes_unchanged']: record['status'] = 'diagnostic_invalid_identity'
        save()
    print(json.dumps({'receipt': str(receipt), 'sha256': sha(receipt), 'status': record['status']}))
    return 0 if record['status'] == 'diagnostic_completed' else 1


if __name__ == '__main__':
    try:
        result = main()
    except BaseException as error:
        # Preflight/authentication failures occur before the normal phase receipt.
        # Retain a distinct terminal refusal without overwriting any existing run.
        if '--prefix' in sys.argv:
            index = sys.argv.index('--prefix') + 1
            if index < len(sys.argv) and re.fullmatch(r'two-argument-ir-raytrace-(?:counters|paired|official)-r(?:[3-9]|[1-9][0-9]+)-20261010', sys.argv[index]):
                path = DATA / (sys.argv[index] + '.json')
                if not path.exists():
                    with path.open('x', encoding='utf-8') as stream:
                        json.dump(dict(terminal=True, status='preflight_failed_no_children', error=repr(error),
                                       phases=[], timing_scoring_permitted=False, controller_sha256=sha(__file__)), stream, indent=2)
                        stream.write('\n')
        raise
    raise SystemExit(result)
