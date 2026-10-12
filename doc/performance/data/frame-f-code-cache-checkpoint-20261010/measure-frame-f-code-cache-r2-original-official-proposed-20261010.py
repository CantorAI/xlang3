"""Original coverage/raytrace fast20: accepted R7b X versus combined X, unpaired."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
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
BENCHMARK_ROOT = CP.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks'
BASE = ROOT / 'scratch/performance/run-gc-r7b-all97-per-definition-ledger-proposed-20261009.py'
OWNERSHIP = ROOT / 'scratch/performance/run-gc-r7b-all97-ownership-supplement-r3-proposed-20261009.py'
OFFICIAL = ROOT / 'scratch/performance/measure-two-argument-ir-original-raytrace-r4-proposed-20261010.py'
GATE_CONTROLLER = ROOT / 'scratch/performance/check-frame-f-code-cache-r2-fixed-gate-proposed-20261010.py'
FROZEN = {
    BASE: '48fafce1a804f1970c63e20ce47c66f593686d2df98f2c678ea8a56ec987e4b6',
    OWNERSHIP: '517113f30f85dc73c62434865e564e2bca6a21331ef988dc438f10ceb4416f53',
    OFFICIAL: '89f62dbde84ef785d555b7f3f28cfe20595fba1a95febc653dcf9fe6c8b44967',
    GATE_CONTROLLER: '4f19f891f1829d3a915a550aa4c9d5a4f5d64870e78960e3c9b96b222974bf47',
}
DEFINITIONS = {
    'coverage': ('19da183fabd314c1374a9bbc541142300669d30d2b9e129d4378dc836b181de6',
                 'a13963c75f871130a042e485abf9b171c77b70e339ddb074a9fb47defc1f0a77'),
    'raytrace': ('88ef4d9060d8e8f6ce40f376477aaf89cc808fa44813225a3071a05a1467f017',
                 'b003d38fa3d472ac4a67a0e692160ba3e87c0b2dc60b94737767a79da45f159c'),
}
CASES = ('local_slots', 'scalar_arithmetic', 'range_for', 'function_calls', 'class_construct',
         'list_append', 'property_access', 'deepcopy_memo', 'json_dumps', 'gc_traversal', 'subparsers')

def require(value, message):
    if not value:
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
            and sys.getprofile() is None, 'Exact isolated unoptimized CPython3.14.7 without observers required')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('benchmark', choices=('coverage', 'raytrace'))
    for label in ('application', 'build', 'focus', 'correctness', 'gate'):
        parser.add_argument('--' + label, type=Path, required=True)
        parser.add_argument('--' + label + '-sha256', required=True)
    parser.add_argument('--prefix')
    parser.add_argument('--dormant-worker-proof', type=Path)
    parser.add_argument('--dormant-worker-proof-sha256')
    args = parser.parse_args()
    args.prefix = args.prefix or ('frame-f-code-cache-r2-original-' + args.benchmark + '-20261010')
    require(re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]+', args.prefix), 'Unsafe prefix')
    require(not any(DATA.glob(args.prefix + '*')), 'Preserve prior attempt; use a new prefix after root review')
    require(bool(args.dormant_worker_proof) == bool(args.dormant_worker_proof_sha256), 'Worker proof/path SHA pair required')
    sys.dont_write_bytecode = True
    # The borrowed phase implements only its original official path here.
    # CLI exposes no counter, replica, warmup shortcut or body-prefilter mode.
    args.mode = 'official'
    pins, trees = {}, {}
    receipt = DATA / (args.prefix + '.json')
    record = dict(terminal=False, passed=False, status='preflight', benchmark=args.benchmark,
        controller_sha256=sha(__file__), phases=[], numeric_parent_accepted=False,
        no_auto_retry=True, full97_refreshed=False, cp_comparison=False,
        scope='Sequential original official fast20 accepted-R7b X versus combined-source148 X. Unpaired; no isolated cache attribution, numeric-parent acceptance or whole-suite gain claim.')
    head = '46496cf5ddb903e25927cce616c29c2f81032566'
    def save():
        receipt.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
    def track(path, expected=None):
        path = Path(path).resolve(strict=True)
        digest = sha(path)
        require(expected is None or digest == expected, 'Input hash differs: ' + str(path))
        require(str(path) not in pins or pins[str(path)] == digest, 'Conflicting input pin')
        pins[str(path)] = digest
        return digest

    try:
        track(__file__)
        for path, digest in FROZEN.items():
            track(path, digest)
        base = load(BASE, 'frame_cache_official_base')
        base.OwnedProcesses = load(OWNERSHIP, 'frame_cache_official_ownership').ownership_class(base)
        official = load(OFFICIAL, 'frame_cache_official_primitives')
        receipts = {}
        for label in ('application', 'build', 'focus', 'correctness', 'gate'):
            path = getattr(args, label).resolve(strict=True)
            track(path, getattr(args, label + '_sha256'))
            receipts[label] = read(path)
        app, build, focus, correctness, validated_gate = (receipts[n] for n in ('application', 'build', 'focus', 'correctness', 'gate'))
        require(app['terminal'] and app['passed'] and app['status'] == 'applied' and app['source_count'] == 148
                and app['numeric_parent_accepted'] is False, 'Wrong combined application')
        require(build['terminal'] and build['passed'] and build['status'] == 'build_passed' and build['exit_code'] == 0
                and build['owned_child_cleanup_completed'] and build['sources_unchanged']
                and build['application_sha256'] == args.application_sha256, 'Build not passed')
        sources, release = app['source_sha256'], build['release_sha256']
        require(len(sources) == 148 and len(release) == 178 and build['source_sha256'] == sources, 'Candidate map mismatch')
        require(focus['terminal'] and focus['passed'] and focus['inputs_unchanged']
                and focus['application_sha256'] == args.application_sha256 and focus['build_sha256'] == args.build_sha256, 'Combined focus not passed')
        require(correctness['terminal'] and correctness['passed'] and correctness['correctness_passed']
                and correctness['hashes_unchanged'] and correctness['application_sha256'] == args.application_sha256
                and correctness['build_sha256'] == args.build_sha256
                and correctness['combined_semantic_receipt_sha256'] == args.focus_sha256, 'Combined correctness not passed')
        require(validated_gate['terminal'] and validated_gate['passed'] and validated_gate['fixed_gate_passed']
                and validated_gate['hashes_unchanged'] and validated_gate['controller_sha256'] == FROZEN[GATE_CONTROLLER]
                and validated_gate['correctness_receipt_sha256'] == args.correctness_sha256
                and validated_gate['application_sha256'] == args.application_sha256
                and validated_gate['build_sha256'] == args.build_sha256
                and validated_gate['combined_semantic_receipt_sha256'] == args.focus_sha256
                and validated_gate['source_sha256'] == sources and validated_gate['release_sha256'] == release,
                'Fresh same-combined default gate missing')
        require(validated_gate['hashes_before'] == validated_gate['hashes_after'], 'Gate input drift')
        for path, digest in validated_gate['hashes_before'].items():
            track(path, digest)  # Historical source145 pins already point into preserved archive146.
        require(len(validated_gate['phases']) == 1, 'Unexpected timed gate phase set')
        phase_gate = validated_gate['phases'][0]
        require(phase_gate['name'] == 'fixed-gate' and phase_gate['passed'] and phase_gate['exit_code'] == 0
                and phase_gate['timed'] and phase_gate['timing_accepted']
                and phase_gate['owned_child_cleanup_completed'] and phase_gate['hashes_unchanged']
                and phase_gate['external_process_watch']['measurement_valid']
                and phase_gate['external_process_watch']['observation_valid']
                and phase_gate['external_process_watch']['post_guard_passed'], 'Gate activity/cleanup invalid')
        for kind in ('stdout', 'stderr'):
            track(DATA / phase_gate[kind], phase_gate[kind + '_sha256'])
        track(DATA / phase_gate['external_process_watch']['log'], phase_gate['external_process_watch']['sha256'])
        gate_json = DATA / validated_gate['fixed_gate_path']
        track(gate_json, validated_gate['fixed_gate_sha256'])
        gate_report = read(gate_json)
        require(gate_report['status'] == 'pass' and set(gate_report['cases']) == set(CASES)
                and gate_report['repeats'] == 21 and gate_report['warmup'] == 5 and gate_report['threshold'] == .10,
                'Complete default11 gate settings differ')
        require(all(c['status'] == 'pass' and all(len(a['baseline_seconds']) == len(a['candidate_seconds']) == 21
                    for a in c['attempts']) for c in gate_report['cases'].values()), 'Default gate arrays incomplete')
        manifest = Path(app['accepted_control_manifest_path']).resolve(strict=True)
        track(manifest, app['accepted_control_manifest_sha256'])
        require(sha(manifest) == 'aa2d9f715ad11e78ffa58024c10631c37e7d83d816c4a8cb0523f6469287547f', 'Accepted R7b control differs')
        control, control_root = read(manifest), manifest.parent
        require(control['full_validated'] and control['fixed_gate_passed'] and len(control['source_sha256']) == 143
                and len(control['release_sha256']) == 178 and app['fixed_baseline_sha256'] == control['fixed_baseline_sha256'], 'Accepted control/baseline mismatch')
        for label, directory, bytecode, expected in (
            ('candidate_release', RELEASE, False, release), ('fixed_baseline', BASELINE, False, app['fixed_baseline_sha256']),
            ('accepted_release', control_root / 'Release', False, control['release_sha256']),
            ('accepted_sources', control_root / 'sources', False, control['source_sha256']),
            ('selected_original_definition', BENCHMARK_ROOT / ('bm_' + args.benchmark), True, None),
            ('dependency_site', SITE, True, None), ('manager_pyperf', CP.parent / 'Lib/site-packages/pyperf', True, None),
            ('manager_pyperformance', CP.parent / 'Lib/site-packages/pyperformance', True, None)):
            values = base.tree_hashes(directory, bytecode)
            require(expected is None or values == expected, 'Protected tree mismatch: ' + label)
            trees[label] = dict(directory=str(directory), exclude_bytecode=bytecode, sha256=values)
            for name, digest in values.items():
                track(directory / name, digest)
        for mapping in (sources, app['protected_test_input_sha256'], app['unowned_tracked_dirty_sha256']):
            for name, digest in mapping.items():
                track(ROOT / name, digest)
        source = BENCHMARK_ROOT / ('bm_' + args.benchmark) / 'run_benchmark.py'
        track(source, DEFINITIONS[args.benchmark][0])
        track(source.with_name('pyproject.toml'), DEFINITIONS[args.benchmark][1])
        for path, digest in ((base.RUNNER, base.RUNNER_SHA), (base.HOOK, base.HOOK_SHA), (base.PARTIAL, base.PARTIAL_SHA),
            (base.WATCH, base.WATCH_SHA), (CP, '4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9'),
            (CP.with_name('python314.dll'), '0f9857ffdfe010fe6b99328d58c2e3c7472ce75f336bf9c2ad9bd5bca3bce700')):
            track(path, digest)
        head = app['head']
        require(head == '46496cf5ddb903e25927cce616c29c2f81032566', 'Application HEAD differs')
        proof_watcher = original_proof_classify = None
        proof_admission = dict(status='no_worker_exception_requested', exception_used=False)
        if args.dormant_worker_proof:
            proof_watcher = load(base.WATCH, 'frame_cache_official_one_worker_baseline')
            proof_admission = official.bind_dormant_worker_proof(args.dormant_worker_proof,
                args.dormant_worker_proof_sha256, proof_watcher, base, track)
            original_proof_classify = proof_watcher.classify
        record.update(application_sha256=args.application_sha256, build_sha256=args.build_sha256,
            focus_sha256=args.focus_sha256, correctness_sha256=args.correctness_sha256, gate_sha256=args.gate_sha256,
            source_sha256=sources, release_sha256=release, accepted_source_sha256=control['source_sha256'],
            accepted_release_sha256=control['release_sha256'], baseline_sha256=app['fixed_baseline_sha256'],
            head=head, pins=pins, input_trees=trees, dormant_worker_admission=proof_admission,
            original_source_sha256=DEFINITIONS[args.benchmark][0],
            provenance_limit='Recorded source148 subset plus Release178; selected original definition and complete non-bytecode dependency/manager package trees pinned now. Not a complete compiler-input or CPython stdlib archive. Canonical-code/assigned metadata failures remain separate, unrepaired.')
        env = os.environ.copy()
        for key in tuple(env):
            if key.startswith(('PYTHON', 'XLANG3_')) or key in ('_NT_SYMBOL_PATH', '_NT_ALT_SYMBOL_PATH'):
                env.pop(key, None)
        env.update(PYTHONPATH=os.pathsep.join((str(base.HOOK.parent), str(SITE))),
            XLANG3_PYTHON_LIB=str(CP.parent / 'Lib'), PYTHONIOENCODING='utf-8',
            PYTHONUNBUFFERED='1', PYTHONDONTWRITEBYTECODE='1')
        record['child_environment'] = {k: env[k] for k in env if k.startswith(('PYTHON', 'XLANG3_'))}
        save()

        def phase(name, command, output, counter_mode=False):
            row = dict(name=name, command=list(map(str, command)), valid=False, counter_mode=counter_mode)
            record['phases'].append(row); save()
            if proof_watcher is None:
                watcher, owned, admission, classify = base.make_watcher(args.prefix, name, None, None)
            else:
                # Fresh owned child tree, ONE policy module/CPU baseline for every phase.
                watcher = proof_watcher
                manager = next(p for p in watcher.scan() if p['ProcessId'] == os.getpid())
                owned = base.OwnedProcesses(manager)
                admission = proof_admission
                def classify(rows, include_runtimes=False, manager_only=False):
                    current_worker = next((p for p in rows if p['ProcessId'] == admission['worker']['ProcessId']), None)
                    require(current_worker is None or watcher._same_dormant(current_worker, rows),
                            'Proof-pinned dormant worker changed/activity/PID reuse')
                    if current_worker is not None:
                        admission['exception_used'] = True
                    result = original_proof_classify(rows, include_runtimes=False)
                    allowed = owned.allowed(rows, manager_only=manager_only)
                    for process in rows:
                        if process['Name'].lower().startswith(('python', 'xlang3')):
                            if (process['ProcessId'], process.get('CreationDate')) not in allowed:
                                result['busy'].append(base.process_identity(process))
                    return result
                watcher.classify = classify
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
                require(metadata.get('name') == args.benchmark and metadata.get('unit') == 'second', 'Original name/unit differ')
                if args.benchmark == 'raytrace':
                    require(metadata.get('raytrace_width') == 100 and metadata.get('raytrace_height') == 100, 'Original raytrace dimensions differ')
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
                row['official_values_accepted'] = row['valid'] and not counter_mode
                row['activity_policy'] = ('Unscored process-local counters: retain all strict activity flags/overlaps; foreign CPU activity is not a counter-semantic failure'
                                          if counter_mode else 'Strict timing idle/watch; only one proof-pinned continuously unchanged orphan node may be dormant; all other activity invalidates timing')
                save()
            require(row['valid'], 'Invalid attempt retained; stop without retry: ' + name)
            print(name + ' PASS', flush=True)
            return row

        def run(label):
            exe = (control_root / 'Release' if label == 'control' else RELEASE) / 'xlang3.exe'
            result = DATA / (args.prefix + '-' + label + '.pyperf.json')
            command = [CP, base.RUNNER, '--runtime', exe, '--benchmarks', args.benchmark, '--mode', 'fast',
                       '--case-timeout', '300', '--dependency-site', SITE, '--output', result]
            return phase(label, command, result)
        old, new = run('control'), run('candidate')
        record.update(passed=True, status='original_official_completed', official_summary=dict(
            control_mean_seconds=old['mean_seconds'], candidate_mean_seconds=new['mean_seconds'],
            ratio_control_over_candidate=old['mean_seconds'] / new['mean_seconds'], values_per_runtime=20,
            limit='Sequential original fast20, accepted-R7b X / combined X. Unpaired; no CP comparison, cache-only causal attribution or whole-suite score.'))
    except BaseException as error:
        record.update(passed=False, status='original_official_failed_or_invalid', error=repr(error))
    finally:
        if trees:
            try:
                record['terminal_identity'] = base.stable(pins, trees, head)
                record['hashes_unchanged'] = record['terminal_identity']['passed']
            except BaseException as error:
                record.update(hashes_unchanged=False, terminal_identity_error=repr(error))
        else:
            record['hashes_unchanged'] = all(Path(p).is_file() and sha(p) == digest for p, digest in pins.items())
        if not record['hashes_unchanged']:
            record.update(passed=False, status='original_official_invalid_identity')
        record['terminal'] = True
        save()
    print(record['status'], sha(receipt), flush=True)
    return 0 if record['passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
