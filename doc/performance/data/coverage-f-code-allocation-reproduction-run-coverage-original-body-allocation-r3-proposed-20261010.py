"""Root-only untimed CP/X original Coverage allocation diagnostic; no builds."""
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
CONTROL = ROOT / 'build-repro/controls/gc-generic-cycles-r7b-accepted-20261009/provenance.json'
CONTROL_SHA = 'aa2d9f715ad11e78ffa58024c10631c37e7d83d816c4a8cb0523f6469287547f'
RESTORE = DATA / 'vm-active-code-view-rejected-20261010.json'
RESTORE_SHA = '92ad1b231b87c847cfdd49f89262ca5bccd280f7c4ed6a83253347768475f214'
HARNESS = DATA / 'gc-r7b-full-ctest-harness-repair-20261009-receipt.json'
HARNESS_SHA = 'cb04a01c52c2fc406816e1cb63896c4f463e414a354ebf62dc845bf77ca4f38f'
PROOF = ROOT / 'scratch/performance/coverage-original-body-allocation-provenance-r3-proposed-20261010.json'
CHILD = ROOT / 'scratch/performance/coverage-original-body-allocation-child-r3-proposed-20261010.py'
PRODUCER = ROOT / 'scratch/performance/run-gc-r7b-all97-ownership-supplement-r3-proposed-20261009.py'
PRODUCER_SHA = '517113f30f85dc73c62434865e564e2bca6a21331ef988dc438f10ceb4416f53'
BUNDLE = ROOT / 'scratch/performance/gc-r7b-ownership-supplement-bundle-r3-proposed-20261009.json'
BUNDLE_SHA = '5525ddbd30ed6ae93c065298fbd3182ae37db1d3fc114d82422934fb2a8926eb'


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def require(value, message):
    if not value:
        raise RuntimeError(message)


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def main():
    require(sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CP.resolve()
            and not sys.flags.optimize, 'Use fixed unoptimized CPython3.14.7')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--proof-sha256', required=True)
    parser.add_argument('--head', required=True)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    require(re.fullmatch(r'coverage-f-code-allocation-[A-Za-z0-9_-]+', args.prefix), 'Unsafe prefix')
    require(not any(DATA.glob(args.prefix + '*')), 'Existing capture: no retry or overwrite')
    require(sha(PROOF) == args.proof_sha256 and sha(CONTROL) == CONTROL_SHA
            and sha(RESTORE) == RESTORE_SHA and sha(HARNESS) == HARNESS_SHA,
            'Proof/control/restoration/harness differs')
    proof, control, restored = read(PROOF), read(CONTROL), read(RESTORE)
    require(restored['terminal'] and restored['status'] == 'rejected_no_demonstrated_gain_restored'
            and restored['unowned_unchanged'] and restored['fixed_baseline_unchanged'], 'Restoration incomplete')
    require(sha(PRODUCER) == PRODUCER_SHA and sha(BUNDLE) == BUNDLE_SHA, 'Frozen guard differs')
    spec = importlib.util.spec_from_file_location('coverage_allocation_guard', PRODUCER)
    producer = importlib.util.module_from_spec(spec); spec.loader.exec_module(producer)
    common, _, _ = producer.load_common(BUNDLE, BUNDLE_SHA)
    base, _, _ = common.primitives()
    base.OwnedProcesses = producer.ownership_class(base)
    pins = {str(Path(p).resolve()): h for p, h in proof['file_sha256'].items()}
    pins.update({str(PROOF): args.proof_sha256, str(CONTROL): CONTROL_SHA,
                 str(RESTORE): RESTORE_SHA, str(HARNESS): HARNESS_SHA})
    require(len(control['source_sha256']) == 143 and len(control['release_sha256']) == 178
            and len(control['fixed_baseline_sha256']) == 177, 'Accepted population differs')
    harness = read(HARNESS)
    require(harness['terminal'] and harness['passed'] and harness['exit_code'] == 0
            and harness['hashes_unchanged'], 'Committed harness repair was not passed')
    repair = harness['harness_source_sha256']
    ps1, debugpy = 'tests/run_fixtures.ps1', 'tests/cli/run_debugpy_launch_smoke.py'
    require(set(repair) == {ps1, debugpy}
            and restored['restored_source_sha256'][ps1] == repair[ps1], 'Restored PS1 repair differs')
    sources = dict(control['source_sha256'])
    sources[ps1] = repair[ps1]  # Sole old143 source exception: committed UTF8 harness repair.
    for name, value in restored['restored_source_sha256'].items():
        require(sources.get(name) == value, 'Restored source differs beyond the declared PS1 repair')
    committed = subprocess.run(['git', 'show', args.head + ':' + ps1], cwd=ROOT,
                               capture_output=True, check=True).stdout
    require(committed.replace(b'\r\n', b'\n') == (ROOT / ps1).read_bytes().replace(b'\r\n', b'\n'),
            'Working PS1 differs from the committed repair')
    require(sha(ROOT / debugpy) == repair[debugpy], 'Protected debugpy repair differs')
    pins[str(ROOT / debugpy)] = repair[debugpy]
    for directory, values in ((ROOT, sources), (RELEASE, control['release_sha256']),
                              (ROOT / 'build-repro/Release', control['fixed_baseline_sha256'])):
        for name, value in values.items():
            path = base.contained(directory, name)
            require(str(path) not in pins or pins[str(path)] == value, 'Conflicting accepted pin')
            pins[str(path)] = value
    require(restored['restored_release_sha256'] == control['release_sha256'], 'Restored Release differs')
    trees = {'release': {'directory': str(RELEASE), 'exclude_bytecode': False, 'sha256': control['release_sha256']},
             'baseline': {'directory': str(ROOT / 'build-repro/Release'), 'exclude_bytecode': False,
                          'sha256': control['fixed_baseline_sha256']}}
    record = {'status': 'preflight', 'terminal': False, 'scored': False, 'phases': [],
              'controller_sha256': sha(__file__), 'proof_sha256': args.proof_sha256,
              'head': args.head, 'source_count': 143, 'release_count': 178, 'fixed_baseline_count': 177,
              'source_exception_sha256': {ps1: repair[ps1]}, 'protected_test_input_sha256': {debugpy: repair[debugpy]},
              'scope': 'One CP then one X original bench_coverage(1), untimed diagnostic; no engine edits/builds.'}
    output = DATA / (args.prefix + '.json')
    def save():
        output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
    save()
    env = os.environ.copy()
    for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONHOME', 'PYTHONOPTIMIZE',
                 'XLANG3_VM_OPCODE_TIMING', 'COVERAGE_CORE'):
        env.pop(name, None)
    env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
    env['PYTHONIOENCODING'] = 'utf-8'
    try:
        for selection, exe in (('cpython', CP), ('xlang3', RELEASE / 'xlang3.exe')):
            command = [str(exe), *(['-I'] if selection == 'cpython' else []), str(CHILD), selection]
            row = {'name': selection, 'command': command, 'cap_seconds': 120,
                   'scored': False, 'timing_scoring_permitted': False, 'count_valid': False}
            record['phases'].append(row); save()
            watcher, owned, _, classify = base.make_watcher(args.prefix, selection, None, None)
            child = finish = None
            stdout, stderr = (DATA / (args.prefix + '-' + selection + suffix) for suffix in ('.stdout.log', '.stderr.log'))
            def idle():
                result = classify(watcher.scan(), manager_only=True)
                return {'passed': not result['busy'], **result}
            try:
                row['pre_identity'] = base.stable(pins, trees, args.head)
                require(row['pre_identity']['passed'], 'Input identity changed')
                try: row['pre_idle'] = idle()
                except BaseException as error: row['activity_observation_error'] = repr(error)
                finish = watcher.start_timing_process_watch(args.prefix, selection, row)
                with stdout.open('xb') as out, stderr.open('xb') as err:
                    child = subprocess.Popen(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                        stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
                    try: owned.seed_child(child, watcher.scan())
                    except BaseException as error: row['child_identity_observation_error'] = repr(error)
                    row['exit_code'] = child.wait(timeout=120)
            except BaseException as error:
                row['error'] = repr(error)
            finally:
                try:
                    if child is not None and child.poll() is None:
                        subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=10)
                        if child.poll() is None: child.kill()
                        child.wait(timeout=10)
                    row['cleanup_passed'] = child is None or child.poll() is not None
                except BaseException as error:
                    row['cleanup_error'] = repr(error)
                for key, action in (('measurement_valid', lambda: finish() if finish else False),
                                    ('post_idle', idle), ('post_identity', lambda: base.stable(pins, trees, args.head))):
                    try: row[key] = action()
                    except BaseException as error: row[key + '_error'] = repr(error)
                row['raw_sha256'] = {str(p): sha(p) for p in (stdout, stderr) if p.is_file()}
                watch = row.get('external_process_watch', {})
                if watch.get('log'):
                    watch_path = base.contained(DATA, watch['log'])
                    row['raw_sha256'][str(watch_path)] = sha(watch_path)
                save()
            # Other processes cannot change this child's private DLL counters.
            # Preserve negative activity evidence; it never authorizes timing.
            require(not any(k in ('error', 'cleanup_error', 'post_identity_error') for k in row)
                and row.get('exit_code') == 0 and row.get('cleanup_passed')
                and row.get('post_identity', {}).get('passed'),
                'Diagnostic child failed/invalid')
            require(stderr.read_bytes() == b'', 'Unexpected stderr')
            result = read(stdout)
            require(result['status'] == 'completed_untimed_original_coverage_allocation_diagnostic'
                and result['selection'] == selection and not result['scored'] and result['bench_coverage_calls'] == 1
                and result['loops'] == 1 and result['trace_restored'] and result['profile_restored']
                and result['counter_enabled_restored'] and not result['counter_reset'], 'Child contract differs')
            row.update(passed=True, count_valid=True, result=result); save()
        cp, x = (row['result'] for row in record['phases'])
        require(all(cp[k] == x[k] for k in ('benchmark_sha256', 'coverage_version', 'fibonacci_10',
                    'return_duration_valid', 'trace_restored', 'profile_restored')), 'CP/X output/tracing parity differs')
        require(cp['counts'] is None and cp['requested_runtime_path'] is None
            and x['requested_runtime_path'] == (RELEASE / 'xlang3_runtime.dll').as_posix()
            and x['excluded_calibration']['passed']
            and x['excluded_calibration']['same_process_runtime_counter_bank_verified']
            and set(x['counts']) == {'Code', 'Frame'}, 'Direct pointer-return counter proof incomplete')
        record.update(status='completed_untimed_counter_diagnostic', output_tracing_parity=True)
    except BaseException as error:
        record.update(status='failed_or_invalid_untimed_diagnostic', error=repr(error))
    finally:
        record['terminal'] = True; save()
    print(json.dumps({'status': record['status'], 'receipt': str(output), 'receipt_sha256': sha(output)}))
    return 0 if record['status'] == 'completed_untimed_counter_diagnostic' else 1


if __name__ == '__main__':
    raise SystemExit(main())
