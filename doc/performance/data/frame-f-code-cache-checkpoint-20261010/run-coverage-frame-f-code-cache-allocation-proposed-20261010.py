"""Root-only current f_code-cache allocation eligibility; unchanged Coverage body."""
# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BASELINE = ROOT / 'build-repro/Release'
SCRATCH = ROOT / 'scratch/performance'
PROOF = SCRATCH / 'coverage-frame-f-code-cache-allocation-provenance-proposed-20261010.json'
CHILD = SCRATCH / 'coverage-original-body-allocation-child-r3-proposed-20261010.py'
APP = DATA / 'frame-f-code-cache-r2-trial-20261010-application.json'
APP_SHA = '37f0cc99feec21b72481a257b287e92fb04ceb047f309aaaa6fe801338390f3a'
BUILD = DATA / 'frame-f-code-cache-r2-trial-20261010-build.json'
BUILD_SHA = '249f35338294c2e6a8e822ed107db3035e2c50b47b5dd46acdbfed26c607a081'
FOCUS = DATA / 'frame-f-code-cache-r2-candidate-semantic-20261010.json'
FOCUS_SHA = 'dc9c6fd38888dbecb9ee4ef4be7d0d087a87d2a6414961f25086ced67eabcfb4'
OLD = DATA / 'coverage-f-code-allocation-r3-20261010.json'
OLD_SHA = 'fa009691724f782fe619d519b91c62eeda209a03563648496b55c7d0693f010d'
BACKEND = DATA / 'coverage-backend-untimed-20261009.json'
BASE = SCRATCH / 'run-gc-r7b-all97-per-definition-ledger-proposed-20261009.py'
OWNERSHIP = SCRATCH / 'run-gc-r7b-all97-ownership-supplement-r3-proposed-20261009.py'


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def require(value, message):
    if not value:
        raise RuntimeError(message)


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    require(sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CP.resolve()
            and sys.flags.isolated and not sys.flags.optimize, 'Use fixed CPython3.14.7 -I without optimization')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--proof-sha256', required=True)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    require(re.fullmatch(r'coverage-frame-f-code-cache-allocation(?:-r[2-9][0-9]*)?-20261010', args.prefix), 'Unsafe prefix')
    require(not any(DATA.glob(args.prefix + '*')), 'Existing attempt: no overwrite or automatic retry')
    output = DATA / (args.prefix + '.json')
    record = dict(status='preflight', terminal=False, passed=False, scored=False,
                  timing_scoring_permitted=False, controller_sha256=sha(__file__), phases=[],
                  proof_sha256=args.proof_sha256, application_sha256=APP_SHA,
                  build_sha256=BUILD_SHA, focused_semantics_sha256=FOCUS_SHA,
                  historical_count_receipt_sha256=OLD_SHA,
                  scope='One CP then one current X unchanged bench_coverage(1); process-local allocation counts only')
    def save():
        output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
    save()
    try:
        require(sha(PROOF) == args.proof_sha256, 'Proposal proof differs')
        proof = read(PROOF)
        pins = {}
        def track(path, expected):
            path = str(Path(path).resolve(strict=True))
            require(sha(path) == expected, 'Input hash differs: ' + path)
            require(path not in pins or pins[path] == expected, 'Conflicting authenticated pin')
            pins[path] = expected
        for path, value in proof['file_sha256'].items():
            track(path, value)
        track(PROOF, args.proof_sha256)
        for path, value in ((APP, APP_SHA), (BUILD, BUILD_SHA), (FOCUS, FOCUS_SHA), (OLD, OLD_SHA)):
            track(path, value)
        app, build, focus, old = map(read, (APP, BUILD, FOCUS, OLD))
        require(app['terminal'] and app['passed'] and app['status'] == 'applied'
                and app['source_count'] == len(app['source_sha256']) == 148, 'Current application is not complete148')
        require(build['terminal'] and build['passed'] and build['status'] == 'build_passed'
                and build['exit_code'] == 0 and build['owned_child_cleanup_completed'] and build['sources_unchanged']
                and build['application_sha256'] == APP_SHA and build['source_sha256'] == app['source_sha256']
                and len(build['release_sha256']) == 178, 'Current build linkage differs')
        require(focus['terminal'] and focus['passed'] and not focus['scored'] and focus['inputs_unchanged']
                and focus['application_sha256'] == APP_SHA and focus['build_sha256'] == BUILD_SHA,
                'Current registered frame/numeric semantics missing')
        require([row['case'] for row in focus['results']] == ['frame_f_code_cache', 'two_argument_double_ir_plan'],
                'Wrong focused population')
        for row in focus['results']:
            require(row['passed'] and row['exit_code'] == 0 and row['direct_child_waited']
                    and Path(row['command'][0]).resolve() == (RELEASE / 'xlang3.exe').resolve(), 'Focused child invalid')
            for kind in ('stdout', 'stderr'):
                path = (DATA / row[kind]).resolve()
                require(path.is_relative_to(DATA.resolve()), 'Focused raw path escaped')
                track(path, row[kind + '_sha256'])
            require((DATA / row['stderr']).read_bytes() == b'', 'Focused stderr not empty')
        track(app['proposal_path'], app['proposal_sha256'])
        track(app['trial_patch_path'], app['trial_patch_sha256'])
        track(DATA / build['log'], build['log_sha256'])
        track(app['accepted_control_manifest_path'], app['accepted_control_manifest_sha256'])
        archive = Path(app['archive_path']).resolve()
        track(archive / 'provenance.json', app['archive_manifest_sha256'])
        require(old['terminal'] and old['status'] == 'completed_untimed_counter_diagnostic'
                and not old['scored'] and old['output_tracing_parity']
                and [row['name'] for row in old['phases']] == ['cpython', 'xlang3'], 'Historical counts are not valid')
        for row in old['phases']:
            require(row['passed'] and row['count_valid'] and row['exit_code'] == 0
                    and row['cleanup_passed'] and row['post_identity']['passed'], 'Historical count phase invalid')
            for path, value in row['raw_sha256'].items():
                track(path, value)
            stdout = next(Path(p) for p in row['raw_sha256'] if p.endswith('.stdout.log'))
            require(read(stdout) == row['result'], 'Historical parsed counts differ from raw')
        backend = read(BACKEND)
        require(backend['terminal'] and not backend['timed'] and backend['hashes_unchanged']
                and backend['status'] == 'backend_selection_recorded'
                and [row['result']['backend_name'] for row in backend['phases']] == ['CTracer', 'PyTracer'],
                'Historical backend observation differs')
        for row in backend['phases']:
            require(row['exit_code'] == 0 and row['result']['coverage_version'] == '7.3.2'
                    and row['result']['core_override'] is None and not row['result']['timid'], 'Historical backend policy differs')
            for kind in ('stdout', 'stderr'):
                track(DATA / row[kind], row[kind + '_sha256'])
            require(read(DATA / row['stdout']) == row['result'], 'Historical backend raw differs')
        base = load(BASE, 'frame_cache_counter_base')
        producer = load(OWNERSHIP, 'frame_cache_counter_ownership')
        base.OwnedProcesses = producer.ownership_class(base)
        trees = dict(proof['input_trees'])
        for label, directory, values in (
                ('current_release', RELEASE, build['release_sha256']),
                ('fixed_baseline', BASELINE, app['fixed_baseline_sha256'])):
            require(len(values) == (178 if label == 'current_release' else 177), 'Fixed tree population differs')
            trees[label] = dict(directory=str(directory), exclude_bytecode=False, sha256=values)
        for directory, values in ((ROOT, app['source_sha256']), (ROOT, app['unowned_tracked_dirty_sha256']),
                                  (ROOT, app['protected_test_input_sha256'])):
            for name, value in values.items():
                track(base.contained(directory, name), value)
        for spec in trees.values():
            directory = Path(spec['directory']).resolve()
            require(base.tree_hashes(directory, spec['exclude_bytecode']) == spec['sha256'], 'Complete input tree differs')
            for name, value in spec['sha256'].items():
                track(base.contained(directory, name), value)
        for path in proof['absent_paths']:
            require(not Path(path).exists(), 'Previously absent config/import source appeared')
        record.update(head=app['head'], source_count=148, release_count=178, fixed_baseline_count=177,
                      source_sha256=app['source_sha256'], release_sha256=build['release_sha256'],
                      pins=pins, input_trees=trees, historical_backend_reference=backend['phases'],
                      backend_limit=proof['backend_limit'], provenance_limits=proof['provenance_limits'])
        env = os.environ.copy()
        removed = {name: value for name, value in env.items()
                   if name.startswith(('PYTHON', 'XLANG3_', 'COVERAGE_'))}
        for name in removed:
            env.pop(name, None)
        env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
        env['PYTHONIOENCODING'] = 'utf-8'
        record['environment_removed_names'] = sorted(removed)
        record['environment_set'] = {name: env[name] for name in ('XLANG3_PYTHON_LIB', 'PYTHONIOENCODING')}
        def stable():
            result = base.stable(pins, trees, app['head'])
            appeared = [p for p in proof['absent_paths'] if Path(p).exists()]
            result['appeared_absent_paths'] = appeared
            result['passed'] = result['passed'] and not appeared
            return result
        for selection, exe in (('cpython', CP), ('xlang3', RELEASE / 'xlang3.exe')):
            row = dict(name=selection, scored=False, timing_scoring_permitted=False, count_valid=False,
                       command=[str(exe), *(['-I'] if selection == 'cpython' else []), str(CHILD), selection], cap_seconds=120)
            record['phases'].append(row); save()
            watcher, owned, _, classify = base.make_watcher(args.prefix, selection, None, None)
            child = finish = None
            stdout, stderr = (DATA / (args.prefix + '-' + selection + suffix) for suffix in ('.stdout.log', '.stderr.log'))
            def idle():
                result = classify(watcher.scan(), manager_only=True)
                return dict(passed=not result['busy'], **result)
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
                        created = base.creation_seconds(identity['CreationDate'])
                        parents = [p for p in identities if p['ProcessId'] == identity['ParentProcessId']
                                   and base.creation_seconds(p['CreationDate']) <= created]
                        if parents:
                            parent = max(parents, key=lambda p: base.creation_seconds(p['CreationDate']))
                            if (parent['ProcessId'], parent['CreationDate']) in known:
                                known[key] = identity; changed = True
                    if not changed:
                        break
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
                    if key not in known:
                        continue
                    require(base.process_identity(current) == known[key], 'Owned descendant identity changed')
                    handle = kernel.OpenProcess(0x100001, False, current['ProcessId'])
                    if not handle:
                        require(not any((p['ProcessId'], p.get('CreationDate')) == key for p in watcher.scan()),
                                'Cannot open still-live owned descendant')
                        continue
                    try:
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
                        'Owned descendants remain')
            try:
                row['pre_identity'] = stable(); require(row['pre_identity']['passed'], 'Inputs changed before child')
                row['pre_idle'] = idle()  # Busy is retained, not a private-counter semantic failure.
                finish = watcher.start_timing_process_watch(args.prefix, selection, row)
                with stdout.open('xb') as out, stderr.open('xb') as err:
                    child = subprocess.Popen(row['command'], cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                                             stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
                    owned.seed_child(child, watcher.scan()); row['child_identity'] = owned.child_identity
                    row['exit_code'] = child.wait(timeout=120)
                require(row['exit_code'] == 0 and stderr.read_bytes() == b'', 'Diagnostic child failed or emitted stderr')
                result = read(stdout)
                require(result['status'] == 'completed_untimed_original_coverage_allocation_diagnostic'
                        and result['selection'] == selection and not result['scored'] and result['bench_coverage_calls'] == 1
                        and result['loops'] == 1 and result['trace_restored'] and result['profile_restored']
                        and result['counter_enabled_restored'] and not result['counter_reset']
                        and result['coverage_version'] == '7.3.2'
                        and Path(result['coverage_file']).resolve() == Path(proof['coverage_init']).resolve(), 'Child contract differs')
                row['result'] = result
            except BaseException as error:
                row['error'] = repr(error)
            finally:
                try:
                    if child is not None and child.poll() is None:
                        child.kill(); child.wait(timeout=15)  # Held Popen HANDLE, never taskkill /PID.
                    cleanup_descendants()
                    row['cleanup_passed'] = child is not None and child.poll() is not None
                except BaseException as error:
                    row['cleanup_error'] = repr(error)
                for key, action in (('measurement_valid', lambda: finish() if finish else False),
                                    ('post_idle', idle), ('post_identity', stable)):
                    try:
                        row[key] = action()
                    except BaseException as error:
                        row[key + '_error'] = repr(error)
                row['raw_sha256'] = {}
                for path in (stdout, stderr):
                    try:
                        if path.is_file(): row['raw_sha256'][str(path)] = sha(path)
                    except BaseException as error:
                        row['raw_hash_error'] = repr(error)
                watch = row.get('external_process_watch', {})
                try:
                    if watch.get('log'):
                        path = base.contained(DATA, watch['log']); row['raw_sha256'][str(path)] = sha(path)
                except BaseException as error:
                    row['watch_hash_error'] = repr(error)
                row['observed_owned_process_identities'] = owned.snapshot()
                row['count_valid'] = (not any(k.endswith('error') for k in row)
                    and row.get('exit_code') == 0 and row.get('cleanup_passed', False)
                    and row.get('post_identity', {}).get('passed', False) and bool(watch)
                    and not watch.get('scanner_errors') and 'result' in row)
                row['passed'] = row['count_valid']
                row['activity_policy'] = 'Private process-local counts only: overlaps and original strict measurement flags retained; no timing permission or dormant-worker exception'
                save()
            require(row['count_valid'], 'Invalid count attempt retained; no automatic retry')
        cp, x = (row['result'] for row in record['phases'])
        require(all(cp[k] == x[k] for k in ('benchmark_sha256', 'coverage_version', 'fibonacci_10',
                    'return_duration_valid', 'trace_restored', 'profile_restored')), 'CP/X parity differs')
        require(cp['counts'] is None and cp['requested_runtime_path'] is None
                and x['requested_runtime_path'] == (RELEASE / 'xlang3_runtime.dll').as_posix()
                and x['excluded_calibration']['passed'] and x['excluded_calibration']['same_process_runtime_counter_bank_verified']
                and set(x['counts']) == {'Code', 'Frame'}, 'Safe pointer counter proof incomplete')
        previous = old['phases'][1]['result']
        require(previous['benchmark_sha256'] == x['benchmark_sha256']
                and previous['coverage_version'] == x['coverage_version'] and previous['layout'] == x['layout'],
                'Historical/current worker, package or counter ABI differs')
        record['allocation_comparison'] = {kind: dict(historical=previous['counts'][kind], current=x['counts'][kind],
                allocations_removed=previous['counts'][kind]['allocations'] - x['counts'][kind]['allocations'])
                for kind in ('Code', 'Frame')}
        record.update(status='completed_untimed_counter_diagnostic', passed=True, output_tracing_parity=True,
                      code_allocation_drop_observed=x['counts']['Code']['allocations'] < previous['counts']['Code']['allocations'])
    except BaseException as error:
        record.update(status='failed_or_invalid_untimed_diagnostic', error=repr(error))
    finally:
        record['terminal'] = True; save()
    print(json.dumps(dict(status=record['status'], receipt=str(output), receipt_sha256=sha(output))))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
