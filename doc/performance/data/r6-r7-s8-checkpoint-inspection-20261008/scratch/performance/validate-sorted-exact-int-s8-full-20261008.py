"""Fresh S8 primitive sorted/R7 module binding checkpoint validation and original pure-Python pickle benchmark.

Use exact CPython 3.14.7 with --source-inventory FINAL_APPLIED_SOURCE.json.
Accepts source_sha256 {path: sha} or files {path: {working_sha256: sha}}.
Require the exact current fourteen-phase proof and an original-body diagnostic
with parity and stable hashes, plus a separately qualifying predeclared sorting-paired proof. All full correctness/gate/pickle checks are fresh; C5 is historical provenance only.
No full97, build or CP reference recapture is launched.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CANDIDATE = ROOT / 'build-repro/main-verify-20261006/Release/xlang3.exe'
BASELINE = ROOT / 'build-repro/Release/xlang3.exe'
CPYTHON = Path(r'C:\Python\Python314\python.exe')
CTEST = r'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
SITE = ROOT / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'
CP_FULL = DATA / 'pyperformance-cpython3147-live-eval-full-fast-20261007.json'
CP_PROVENANCE = CP_FULL.with_name(CP_FULL.stem + '-provenance.json')
HOOK = ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
CASES = {'pickle_pure_python': 'bm_pickle'}
OFFICIAL_SUBTESTS = {'pickle_pure_python': ('pickle_pure_python',)}
BODY_CHILD = ROOT / 'scratch/performance/pickle-original-pure-python-diagnostic-proposed-20261008.py'
BODY_CHILD_SHA = '5295c899d95d023ca2fb3b511e9bf78b58cb49e4a256770c65adf0d614b14109'
C5_PARENT = DATA / 'ordinary-canonical-slot-constructor-c5-gate-idle-resume-20261008.json'
C5_PARENT_SHA = 'a3be06b1ef97560253e7f4f0d7740440786f82ca08e0a56b564bfe2bfd3f96ed'
C5_INVENTORY_SHA = '69aaf03539202bbc7fced4dfe46df5f3a801a6d40b8e0360a68245de6ce71937'
C5_BODY = DATA / 'pickle-original-pure-c5-body-20261008.json'
C5_BODY_SHA = '2d6a79f263d3e20f86f494bc13dfef795e395fba0b81773a2bc238472d811665'
C5_SAMPLE = DATA / 'pickle-original-pure-c5-native-sampling-20261008.json'
C5_SAMPLE_SHA = 'e5919bc0ec2840d3300888f1fb54dd10ac66cb96a66734d47a05322d26f5a0a7'
C5_CONTROL = ROOT / 'build-repro/controls/class-constructor-c5-before-snapshot-r6-20261008'
C5_CONTROL_MANIFEST_SHA = '85910ee375f16c677153fc128fb17303868a849c124f5909d5ee03eeed764bdc'
THREAD_SOURCE = ROOT / 'tests/fixtures/core/threading_runtime_edges.py'
THREAD_SOURCE_SHA = '56016bd84f76782ce736058670c50035c8a1600ac67fca201ef1a304e6875c39'
THREAD_EXPECTED = ROOT / 'tests/fixtures/expected/threading_runtime_edges.out'
THREAD_EXPECTED_SHA = '7ac4a64a76bc6207024ea9c001dcfcf336e991effe39b9c08602ceb7c332ba22'
S8_CASES = ('call_module_global_binding', 'sorted_exact_integer_keys')
PURE_DEFINITION_SHA = '846f31a4f830d4b2ab044917d3b3e6b036ace3647445d770c8980f1f5b158c21'
FOCUSED_PHASES = ['sorted7', 'iteration4', 'nested1', 'canonical10', 'fallback3', 'ownership2', 'owner2', 'namespace4', 'profile3', 'annotation2', 'threading', 'module_binding6', 'integer_sort7', 'cpp']
C5_CASES = ['ordinary_canonical_slot_constructor', 'explicit_slot_descriptor_fallback',
            'synchronous_class_argument_lifetime', 'slot_descriptor_owner', 'class_namespace_lifetime', 'nested_profile_setting', 'class_method_annotation_capture']
REFERENCE = DATA / 'heapq-cold-metadata-outline-full-resume-r3-20261008-inherited-full.json'
REFERENCE_SHA = 'c0dde153eadca8989321bfdfe2e6e1083f1491ec34f939658454135697c051b9'
WATCH = ROOT / 'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'
WATCH_SHA = '50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf'
CTEST_NAMES = {
    'xlang3_sdk_stream_call_tests', 'xlang3_graph_producer', 'xlang3_graph_consumer',
    'xlang3_graph_reject_BAD_VERSION', 'xlang3_graph_reject_NO_CODEC',
    'xlang3_graph_reject_DECODE_FAIL', 'xlang3_runtime_value_tests',
    'xlang3_interpreter_tests', 'xlang3_cli_sqlite_module_imports',
}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def normalized(raw):
    return raw.decode('utf-8').replace('\r\n', '\n').rstrip()


def values_for(document, name):
    matches = [row for row in document['benchmarks']
               if row.get('metadata', {}).get('name', document.get('metadata', {}).get('name')) == name]
    assert len(matches) == 1, (name, 'missing/duplicate official benchmark')
    values = [value for run in matches[0]['runs'] for value in run.get('values', [])]
    assert len(values) == 20 and all(math.isfinite(value) and value > 0 for value in values), name
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-inventory', '--inventory', required=True, type=Path)
    parser.add_argument('--source-inventory-sha256', required=True)
    parser.add_argument('--focused-receipt', required=True, type=Path)
    parser.add_argument('--focused-receipt-sha256', required=True)
    parser.add_argument('--body-receipt', required=True, type=Path)
    parser.add_argument('--body-receipt-sha256', required=True)
    parser.add_argument('--body-controller', type=Path, required=True)
    parser.add_argument('--body-controller-sha256', required=True)
    parser.add_argument('--sorting-paired-receipt', type=Path, required=True)
    parser.add_argument('--sorting-paired-receipt-sha256', required=True)
    parser.add_argument('--build-log', type=Path)
    parser.add_argument('--build-log-sha256')
    parser.add_argument('--prefix', default='sorted-exact-int-s8-full-validation-20261008')
    parser.add_argument('--identity-reference', type=Path,
        default=DATA / 'identity-last-use-r2-cpython3147-reference-20261008.json')
    reference = json.loads(REFERENCE.read_text(encoding='utf-8'))
    parser.add_argument('--cache-reference', type=Path, default=DATA / reference['required_cache_reference']['record'],
        help='Successful exact CPython 3.14.7 receipt for the frozen sqlite_statement_cache fixture; no CP execution')
    parser.add_argument('--dict-reference', type=Path, default=DATA / reference['required_dict_reference']['record'],
        help='Successful exact CPython 3.14.7 receipt for the frozen dict-cache-touch fixture')
    parser.add_argument('--constructor-reference', type=Path, default=DATA / reference['required_constructor_reference']['record'],
        help='Successful exact CPython 3.14.7 receipt for the frozen inherited constructor fixture')
    parser.add_argument('--cross-activation-reference', type=Path, default=DATA / reference['required_cross_activation_reference']['record'],
        help='Successful exact CPython 3.14.7 receipt for the seven-group cross-activation fixture')
    args = parser.parse_args()
    assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CPYTHON.resolve()
    assert sys.flags.optimize == 0 and bool(args.build_log) == bool(args.build_log_sha256)
    assert re.fullmatch(r'[a-z0-9-]+', args.prefix), 'Use a fresh simple output prefix'
    output = DATA / (args.prefix + '.json')
    assert not any(DATA.glob(args.prefix + '*')), 'Existing evidence must never be overwritten'
    inventory_path = args.source_inventory.resolve(strict=True)
    inventory = json.loads(inventory_path.read_text(encoding='utf-8'))
    if 'source_sha256' in inventory:
        sources = inventory['source_sha256']
    else:
        sources = {name: row['working_sha256'] for name, row in inventory['files'].items()}
    assert sources and all(re.fullmatch(r'[0-9a-f]{64}', sha) for sha in sources.values())
    assert digest(inventory_path) == args.source_inventory_sha256
    required_sources = {'modules/sqlite/sqlite_package.cpp', 'modules/sqlite/sqlite_values.cpp',
        'modules/sqlite/sqlite_handles.h', 'sdk/xlang3/abi/xmodule.h',
        'src/import/native_package_loader.cpp', 'src/builtins/functional_builtins.cpp',
        'src/runtime/value_hash.cpp', 'tests/cpp/interpreter_tests.cpp'}
    required_sources.update({'modules/sqlite/sqlite_handles.cpp',
        'tests/cpp/sqlite_statement_cache_cases.h', 'tests/cpp/sqlite_statement_cache_sdk_probe.py',
        'tests/fixtures/core/sqlite_statement_cache.py', 'tests/fixtures/expected/sqlite_statement_cache.out',
        'tests/run_fixtures.py', 'tests/run_fixtures.ps1'})
    required_sources.update({
        'src/executor/xlang_vm/ops/xlang_vm_ops_call.h', 'src/runtime/object_model.cpp',
        'src/internal/xlang3/perf_counters.h',
        'tests/cpp/inherited_call_ex_constructor_cases.h',
        'tests/fixtures/core/call_method_dict_cache_touch.py',
        'tests/fixtures/expected/call_method_dict_cache_touch.out',
        'tests/fixtures/core/inherited_call_ex_constructor.py',
        'tests/fixtures/expected/inherited_call_ex_constructor.out'})
    required_sources.update({'src/executor/xlang_vm/xlang_frame.h',
        'tests/fixtures/core/call_ex_cross_activation_constructor.py',
        'tests/fixtures/expected/call_ex_cross_activation_constructor.out'})
    required_sources.update({'src/internal/xlang3/interpreter.h', 'src/internal/xlang3/functional_iterators.h',
        'src/runtime/functional_iterators.cpp', 'src/executor/xlang_vm/xlang_interpreter.cpp',
        'tests/cpp/scoped_sorted_key_cases.h'})
    for name in ('sorted_key_scoped_entry', 'sorted_key_iteration_owner', 'sorted_key_nested_handled_context'):
        required_sources.update({'tests/fixtures/core/' + name + '.py', 'tests/fixtures/expected/' + name + '.out'})
    required_sources.update({'src/internal/xlang3/ir.h', 'src/internal/xlang3/object_model.h',
        'src/executor/xlang_vm/xlang_vm_loop.cpp', 'src/executor/xlang_vm/xlang_vm_inline_call.h',
        'src/executor/xlang_vm/xlang_vm_attr.cpp', 'src/executor/xlang_vm/ops/xlang_vm_ops_attr.h',
        'tests/cpp/ordinary_canonical_slot_constructor_cases.h'})
    for name in S8_CASES:
        required_sources.update({'tests/fixtures/core/' + name + '.py', 'tests/fixtures/expected/' + name + '.out'})
    required_sources.update({'src/runtime/runtime.cpp', 'src/internal/xlang3/pyc_magic.h',
        'tests/cpp/observable_builtin_method_cases.h', 'src/sema/lower.cpp',
        'tests/cpp/class_method_annotation_capture_cases.h'})
    for name in C5_CASES:
        required_sources.update({'tests/fixtures/core/' + name + '.py', 'tests/fixtures/expected/' + name + '.out'})
    assert required_sources <= sources.keys(), 'Supply the complete FINAL compiled source inventory, including both generic repairs and unchanged CPP counter dependency'
    record = {'status': 'running', 'terminal': False, 'started_utc': datetime.now(timezone.utc).isoformat(),
        'scope': 'Fresh S8/R7 full correctness/default gate and original pure-Python pickle attempt; exact current fourteen-phase proof and body parity retained, all full phases fresh; sorting gain has its own predeclared paired prerequisite',
        'full_validated': False,
        'source_inventory': str(inventory_path), 'source_inventory_sha256': digest(inventory_path),
        'source_sha256': sources, 'source_count': len(sources), 'phases': [], 'idle_guards': [],
        'known_limits': ['SQLite registration factory GC edges are not published',
            'Pre-existing scalar registration failure ownership gap is separate',
            'Conservative cursor lookahead guard does not claim unrestricted CPython reentrancy parity',
            'CPython guard diagnostic crashes are retained; no blanket guard parity claim'],
        'official_score_scope': 'Original pickle_pure_python definition/protocol5; saved CP row and fresh X row require20values; no full97/overall CPython-win claim'}
    tracked = {}

    def track(path, expected=None):
        path = Path(path).resolve(strict=True)
        sha = digest(path)
        if expected is not None:
            assert sha == expected, str(path)
        if str(path) in tracked:
            assert tracked[str(path)] == sha, 'Input changed during preflight'
        tracked[str(path)] = sha
        return sha

    def save():
        output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))

    def idle(name):
        deadline = time.monotonic() + 300
        while True:
            command = 'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'
            completed = subprocess.run(['powershell', '-NoProfile', '-Command', command], capture_output=True, check=True)
            rows = json.loads(completed.stdout.decode('utf-8-sig') or '[]')
            if isinstance(rows, dict): rows = [rows]
            tool_names = {'cl.exe', 'link.exe', 'ninja.exe', 'msbuild.exe', 'cmake.exe', 'ctest.exe',
                          'nmake.exe', 'lld-link.exe', 'clang-cl.exe'}
            busy = [row for row in rows if row['ProcessId'] != os.getpid() and
                    (row['Name'].lower() in tool_names or row['Name'].lower().startswith(('python', 'xlang3')))]
            record['idle_guards'].append({'phase': name, 'allowed_controller_pid': os.getpid(), 'busy': busy,
                'observed_utc': datetime.now(timezone.utc).isoformat(), 'wait_limit_seconds': 300,
                'state': 'waiting' if busy else 'idle'})
            save()
            if not busy:
                assert all(Path(path).is_file() and digest(path) == value for path, value in tracked.items())
                return
            remaining = deadline - time.monotonic()
            print('Waiting for idle before', name, 'busy', busy, 'remaining_seconds', max(0, round(remaining, 1)), flush=True)
            assert remaining > 0, ('Idle wait exceeded300seconds', busy)
            time.sleep(min(5, remaining))

    env = os.environ.copy()
    env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
    for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE'):
        env.pop(name, None)
    assert not env.get('XLANG3_VM_OPCODE_TIMING'), 'Official validation must be uninstrumented'

    def phase(name, command, timeout=120, expected=None, required=True):
        idle(name)
        paths = [DATA / (args.prefix + '-' + name + suffix) for suffix in ('.stdout.log', '.stderr.log')]
        assert not any(path.exists() for path in paths)
        print('Starting', name, flush=True)
        row = {'name': name, 'command': list(map(str, command)), 'timeout_seconds': timeout}
        finish_watch = watcher.start_timing_process_watch(args.prefix, name, row) if name == 'fixed-gate' or name.startswith('official-') else None
        measurement_valid = True
        try:
            with paths[0].open('xb') as stdout, paths[1].open('xb') as stderr:
                child = subprocess.Popen(row['command'], cwd=ROOT, env=env, stdout=stdout, stderr=stderr)
                try:
                    row['exit_code'] = child.wait(timeout=timeout)
                    row['timeout'] = False
                except subprocess.TimeoutExpired:
                    cleanup = subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)],
                                             capture_output=True)
                    if child.poll() is None: child.kill()
                    child.wait(timeout=15)
                    row.update(exit_code=124, timeout=True, timeout_cleanup_exit_code=cleanup.returncode,
                               timeout_cleanup_stdout=cleanup.stdout.decode('utf-8', errors='replace'),
                               timeout_cleanup_stderr=cleanup.stderr.decode('utf-8', errors='replace'))
        finally:
            if finish_watch is not None: measurement_valid = finish_watch()
        row.update(stdout_log=paths[0].name, stderr_log=paths[1].name,
                   stdout_sha256=digest(paths[0]), stderr_sha256=digest(paths[1]))
        row.update(log=paths[0].name, sha256=row['stdout_sha256'])
        if expected is not None:
            row['output_matches_expected'] = normalized(paths[0].read_bytes()) == expected
        row['passed'] = row['exit_code'] == 0 and row.get('output_matches_expected', True) and measurement_valid
        record['phases'].append(row)
        record['status'] = 'running' if row['passed'] else 'failed_' + name
        save()
        print('Finished', name, 'exit', row['exit_code'], 'passed', row['passed'], flush=True)
        if required: assert row['passed'], name
        return row, paths[0]

    def cp_reference(path, fixture, expected, original_source=None):
        track(path)
        reference = json.loads(path.read_text(encoding='utf-8'))
        assert reference['exit_code'] == 0 and reference['cpython_version'].startswith('3.14.7 ')
        original_source = Path(reference.get('source') or original_source)
        track(original_source, reference['source_sha256'])
        assert fixture.read_bytes().replace(b'\r\n', b'\n') == original_source.read_bytes().replace(b'\r\n', b'\n')
        stdout_path = path.with_name(path.stem + '.stdout.log')
        stderr_path = path.with_name(path.stem + '.stderr.log')
        track(stdout_path, reference['stdout_sha256'])
        track(stderr_path, reference['stderr_sha256'])
        assert normalized(stdout_path.read_bytes()) == expected
        assert reference.get('output_matches_expected', True)
        return {'record': path.name, 'sha256': digest(path), 'source_sha256': reference['source_sha256'],
                'stdout_log': stdout_path.name, 'stderr_log': stderr_path.name, 'reused_without_execution': True}

    def verify_sorting_paired_receipt():
        import random
        import statistics
        path = args.sorting_paired_receipt.resolve(strict=True)
        track(path, args.sorting_paired_receipt_sha256)
        paired = json.loads(path.read_text(encoding='utf-8'))
        producer = ROOT / 'scratch/performance/measure-sorted-exact-int-s8-paired-20261008.py'
        producer_sha = '5fc9504823e58d81555d39869b75bdb5c8a9a8ec6ad94df00bb7e23e433867c2'
        child = ROOT / 'benchmarks/diagnostics/python_callback_boundary.py'
        child_sha = 'f76136594ac8e64e5fb0d71a47eff3af286d5e12178a8acbaa7fe4c43a6ef84a'
        track(producer, producer_sha); track(child, child_sha)
        assert paired['terminal'] and paired['status'] == 'terminal_unscored_paired_callback_boundary'
        assert paired['hashes_unchanged'] and paired['hashes_before'] == paired['hashes_after']
        assert not paired['scored'] and not paired['full_validated']
        assert paired['controller_sha256'] == producer_sha and paired['child_sha256'] == child_sha
        assert paired['source_inventory_sha256'] == digest(inventory_path) and paired['source_sha256'] == sources
        assert paired['focused_receipt_sha256'] == digest(focused_path)
        assert paired['binaries_sha256'] == record['binaries_sha256']
        assert (paired['pair_count'], paired['total_child_invocations'], paired['total_timed_case_invocations']) == (7, 14, 210)
        assert (paired['warmup_case_invocations_per_child'], paired['timed_samples_per_case_per_child']) == (5, 3)
        assert paired['values_trimmed'] == paired['outliers_removed'] == 0
        assert paired['decision_rule'] == dict(selected_case='sort_plain', minimum_median_ratio=1.10,
            minimum_lower_ci=1.0, bootstrap_resamples=50000, bootstrap_seed=20261008, strict_greater_than=True)
        assert paired['hashes_before'][str(producer)] == producer_sha
        assert paired['hashes_before'][str(child)] == child_sha
        manifest = C5_CONTROL / 'preserved-release-provenance.json'
        assert paired['hashes_before'][str(manifest)] == C5_CONTROL_MANIFEST_SHA
        for pinned_path, sha in paired['hashes_before'].items(): track(Path(pinned_path), sha)
        cases = ('loop', 'small_calls', 'branch_calls', 'sort_plain', 'sort_callback')
        assert len(paired['pair_results']) == 7 and len(paired['raw']) == 14
        ratios, all_rows = [], []
        for index, pair in enumerate(paired['pair_results']):
            order = ['control', 'candidate'] if index % 2 == 0 else ['candidate', 'control']
            assert pair['pair_index'] == index and pair['order'] == order
            assert set(pair['runs']) == {'control', 'candidate'}
            for role in order:
                row = pair['runs'][role]; all_rows.append(row)
                assert row['pair_index'] == index and row['runtime'] == role
                assert row['passed'] and row['exit_code'] == 0 and not row['timeout'] and row['measurement_valid']
                executable = (C5_CONTROL / 'xlang3.exe') if role == 'control' else CANDIDATE
                assert [Path(value).resolve() for value in row['command']] == [executable.resolve(), child.resolve()]
                for stream in ('stdout', 'stderr'): track(DATA / row[stream + '_log'], row[stream + '_sha256'])
                assert (DATA / row['stderr_log']).read_bytes() == b''
                parsed, indices = {case: [] for case in cases}, {case: [] for case in cases}
                lines = (DATA / row['stdout_log']).read_text(encoding='utf-8-sig').splitlines()
                assert len(lines) == 15
                for line in lines:
                    marker, case, sample, count, value = line.split()
                    assert marker == 'callback_boundary' and case in parsed and int(count) == 50000
                    elapsed = float(value)
                    assert math.isfinite(elapsed) and elapsed > 0
                    parsed[case].append(elapsed); indices[case].append(int(sample))
                assert all(values == [0, 1, 2] for values in indices.values()) and row['times'] == parsed
                watch = row['external_process_watch']
                assert watch['measurement_valid']
                track(DATA / watch['log'], watch['sha256'])
            ratio = statistics.median(pair['runs']['control']['times']['sort_plain']) / statistics.median(pair['runs']['candidate']['times']['sort_plain'])
            assert math.isfinite(ratio) and ratio > 0 and pair['ratios']['sort_plain'] == ratio
            ratios.append(ratio)
        assert all_rows == paired['raw'], 'Retain every original child row in predeclared order'
        rng = random.Random(20261008)
        draws = sorted(statistics.median([ratios[rng.randrange(7)] for _ in range(7)]) for _ in range(50000))
        def percentile(q):
            position = q * (len(draws) - 1); lower = math.floor(position); upper = math.ceil(position)
            return draws[lower] + (draws[upper] - draws[lower]) * (position - lower)
        median = statistics.median(ratios); interval = [percentile(.025), percentile(.975)]
        summary = paired['summary']
        assert summary['pair_ratios'] == ratios and summary['median_pair_ratio'] == median
        assert summary['bootstrap95_ci'] == interval
        assert summary['all_cases']['sort_plain'] == dict(pair_ratios=ratios, median_pair_ratio=median, bootstrap95_ci=interval)
        assert median > 1.10 and interval[0] > 1.0 and summary['useful_signal']
        assert summary['decision'] == 'signal_for_full_checkpoint_validation'
        record['required_sorting_paired_diagnostic'] = dict(record=path.name, sha256=digest(path),
            controller_sha256=producer_sha, child_sha256=child_sha, pair_count=7, total_children=14,
            selected_case='sort_plain', pair_ratios=ratios, median_pair_ratio=median, bootstrap95_ci=interval,
            minimum_median_ratio=1.10, minimum_lower_ci=1.0, values_trimmed=0, outliers_removed=0,
            scope='Predeclared seven-pair unchanged diagnostic C5/S8 sorting signal only; no official score, isolated sorting-vs-module causality or CPython win claim')
        save()

    try:
        save()
        track(inventory_path, record['source_inventory_sha256'])
        track(Path(__file__))
        track(REFERENCE, REFERENCE_SHA)
        track(WATCH, WATCH_SHA)
        watcher_spec = importlib.util.spec_from_file_location('sorted_s8_full_timing_watch', WATCH)
        watcher = importlib.util.module_from_spec(watcher_spec)
        watcher_spec.loader.exec_module(watcher)
        if args.build_log:
            build_path = args.build_log.resolve(strict=True)
            record['preserved_build_log'] = {'path': str(build_path), 'sha256': track(build_path, args.build_log_sha256),
                'scope': 'Raw actual build output provided by root; no build launched by this controller'}
        for name, sha in sources.items(): track(ROOT / name, sha)
        # Pin binaries freshly after the parent's build, including all own native modules.
        binary_paths = sorted(path for path in CANDIDATE.parent.rglob('*') if path.is_file())
        assert len(binary_paths) == 178
        assert CANDIDATE in binary_paths and CANDIDATE.with_name('xlang3_runtime.dll') in binary_paths
        record['binaries_sha256'] = {path.relative_to(ROOT).as_posix(): track(path) for path in binary_paths}
        record['candidate_binary_sha256'] = {'exe': digest(CANDIDATE), 'dll': digest(CANDIDATE.with_name('xlang3_runtime.dll'))}
        focused_path = args.focused_receipt.resolve(strict=True)
        track(focused_path, args.focused_receipt_sha256)
        focused = json.loads(focused_path.read_text(encoding='utf-8'))
        assert focused['terminal'] and focused['status'] == 'targeted_correctness_passed' and focused['hashes_unchanged']
        assert focused['source_sha256'] == sources and focused['source_inventory_sha256'] == digest(inventory_path)
        assert focused['candidate_binary_sha256'] == record['candidate_binary_sha256']
        assert focused['binaries_sha256'] == record['binaries_sha256']
        assert len(focused['phases']) == len(FOCUSED_PHASES) and {row['name'] for row in focused['phases']} == set(FOCUSED_PHASES)
        for row in focused['phases']:
            assert row['exit_code'] == 0 and not row.get('timeout', False) and row.get('passed', True)
            if row['name'] != 'cpp': assert row['output_matches_expected']
            for stream in ('stdout','stderr'): track(DATA / row[stream + '_log'], row[stream + '_sha256'])
            if row['name'] != 'cpp': assert (DATA / row['stderr_log']).read_bytes() == b''
        record['retained_current_targeted_proof'] = {'path': str(focused_path), 'sha256': digest(focused_path),
            'phases': [dict(row, execution='retained_historical_same_candidate') for row in focused['phases']],
            'scope': 'Only exact current-final-source/binary/output proof retained; every full phase below is fresh, no F/G/R5 correctness reuse'}
        body_path = args.body_receipt.resolve(strict=True)
        track(body_path, args.body_receipt_sha256)
        assert args.body_controller.resolve(strict=True).is_relative_to(ROOT / 'scratch/performance')
        track(args.body_controller, args.body_controller_sha256)
        track(BODY_CHILD, BODY_CHILD_SHA)
        track(THREAD_SOURCE,THREAD_SOURCE_SHA); track(THREAD_EXPECTED,THREAD_EXPECTED_SHA)
        track(C5_BODY,C5_BODY_SHA); track(C5_PARENT,C5_PARENT_SHA); track(C5_SAMPLE,C5_SAMPLE_SHA)
        body, c5_body, parent = (json.loads(path.read_text(encoding='utf-8')) for path in (body_path,C5_BODY,C5_PARENT))
        assert parent['terminal'] and parent['hashes_unchanged'] and parent['correctness_passed'] and parent['release_tree_unchanged']
        assert parent['status']=='correctness_and_gate_passed_official_failed' and not parent['full_validated']
        assert parent['fixed_gate']['exit_code']==0 and parent['source_inventory_sha256']==C5_INVENTORY_SHA
        assert body['terminal'] and body['status']=='terminal_unscored_original_body_match' and body['hashes_unchanged']
        assert body['mode']=='body' and body['controller_sha256']==args.body_controller_sha256
        assert body['diagnostic_only'] and not body['full_validated'] and not body['profile_enabled'] and body['fixed_gate'] is None
        assert body['child_sha256']==BODY_CHILD_SHA and body['timeout_seconds']==300
        assert (body['outer_loops'],body['protocol'],body['original_dump_operations'])==(41,5,2460)
        assert body['source_inventory_sha256']==digest(inventory_path) and body['source_sha256']==sources
        assert body['focused_receipt_sha256']==digest(focused_path) and len(body['focused_phases'])==len(FOCUSED_PHASES) and set(body['focused_phases'])==set(FOCUSED_PHASES)
        assert body['release_count']==178 and body['binaries_sha256']==record['binaries_sha256']
        assert body['candidate_binary_sha256']==record['candidate_binary_sha256'] and body['hashes_before']==body['hashes_after']
        for path, sha in body['hashes_before'].items(): track(Path(path),sha)
        history=body['c5_parent_reference']
        assert history['sha256']==C5_PARENT_SHA and history['original_body_sha256']==C5_BODY_SHA
        assert history['native_sample_sha256']==C5_SAMPLE_SHA and history['control_manifest_sha256']==C5_CONTROL_MANIFEST_SHA
        assert Path(history['receipt']).resolve()==C5_PARENT.resolve() and Path(history['control']).resolve()==C5_CONTROL.resolve()
        assert c5_body['terminal'] and c5_body['status']=='terminal_unscored_original_body_match' and c5_body['hashes_unchanged'] and c5_body['mode']=='body'
        assert body['result_signature']==c5_body['result_signature'] and c5_body['child_sha256']==BODY_CHILD_SHA
        def body_rows(receipt):
            assert len(receipt['raw'])==2 and all(row['passed'] and row['exit_code']==0 and not row['timeout'] for row in receipt['raw'])
            rows={row['runtime']:row for row in receipt['raw']}
            assert set(rows)=={'cpython3147','candidate-xlang3'}
            for row in rows.values():
                for stream in ('stdout','stderr'): track(DATA/row[stream+'_log'],row[stream+'_sha256'])
                assert (DATA/row['stderr_log']).read_bytes()==b''
                assert math.isfinite(row['elapsed_seconds_diagnostic_only']) and row['elapsed_seconds_diagnostic_only']>0
            return rows
        actual_body, original_body = body_rows(body), body_rows(c5_body)
        speedup=original_body['candidate-xlang3']['elapsed_seconds_diagnostic_only']/actual_body['candidate-xlang3']['elapsed_seconds_diagnostic_only']
        assert math.isfinite(speedup)
        record['required_original_body_diagnostic']={'record':body_path.name,'sha256':digest(body_path),
            'c5_reference':C5_BODY.name,'c5_reference_sha256':C5_BODY_SHA,
            'c5_x_seconds':original_body['candidate-xlang3']['elapsed_seconds_diagnostic_only'],
            's8_x_seconds':actual_body['candidate-xlang3']['elapsed_seconds_diagnostic_only'],
            'historical_unpaired_c5_over_s8':speedup,
            'scope':'Fresh original-body parity and hash-stable correctness prerequisite only; historical ratio is unpaired/unscored and is not a performance acceptance threshold or CP win claim'}
        record['historical_c5_parent']=dict(history,correctness_reused=False)
        save()
        verify_sorting_paired_receipt()
        track(BASELINE, 'a5f5028c15e145edce645a5afc25c11fbce77f51e882312b1fbe06e63c72a4af')
        track(BASELINE.with_name('xlang3_runtime.dll'), 'bc1b9c0a8086f7e6fb0c037516dc9c1eea20427fa887e3aa623714bc5ef5da8d')
        cp_provenance = json.loads(CP_PROVENANCE.read_text(encoding='utf-8'))
        assert cp_provenance['status'] == 'finished' and cp_provenance['runtime_version'] == '3.14.7'
        record['cpython3147_binary_sha256'] = track(CPYTHON, cp_provenance['sha256_start']['exe'])
        record['cpython3147_dll_sha256'] = track(CPYTHON.with_name('python314.dll'), cp_provenance['sha256_start']['dll'])
        record['compatibility_hook_sha256'] = track(HOOK, cp_provenance['compatibility_hook_sha256'])
        track(CP_PROVENANCE)
        track(CP_FULL)
        cp_document = json.loads(CP_FULL.read_text(encoding='utf-8'))
        benchmark_root = CPYTHON.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks'
        record['benchmark_source_sha256'] = {}
        definition=benchmark_root/'bm_pickle/bm_pickle_pure_python.toml'
        track(definition,PURE_DEFINITION_SHA)
        import tomllib
        pure_config=tomllib.loads(definition.read_text(encoding='utf-8'))['tool']['pyperformance']
        assert pure_config['name']=='pickle_pure_python' and pure_config['extra_opts']==['--pure-python','pickle']
        track(CPYTHON.parent/'Lib/pickle.py','144fdf593814627e13a5fcb0947728e8f0775ecdec181046ee4609063cf232ec')
        for name, directory in CASES.items():
            source = benchmark_root / directory / 'run_benchmark.py'
            expected_sha = cp_provenance['benchmark_python_sources'][directory + '\\run_benchmark.py']
            record['benchmark_source_sha256'][name] = track(source, expected_sha)
            for subtest in OFFICIAL_SUBTESTS[name]: values_for(cp_document, subtest)
        def pure_metadata(document):
            matches=[row for row in document['benchmarks'] if row.get('metadata',{}).get('name',document.get('metadata',{}).get('name'))=='pickle_pure_python']
            assert len(matches)==1
            metadata={**document.get('metadata',{}),**matches[0].get('metadata',{})}
            assert metadata['pickle_module']=='pickle' and str(metadata['pickle_protocol'])=='5' and metadata['inner_loops']==20
            values_for(document,'pickle_pure_python')
            return {key:metadata[key] for key in ('name','pickle_module','pickle_protocol','inner_loops')}
        record['pure_python_pickle_definition']={'source':str(definition),'sha256':PURE_DEFINITION_SHA,'extra_opts':pure_config['extra_opts'],
            'preserved_cp_metadata':pure_metadata(cp_document),'scope':'Original pure-Python definition/protocol; saved CP Oct7 comparison is unpaired'}
        record['preserved_official_cpython3147'] = {'output': CP_FULL.name, 'sha256': digest(CP_FULL),
            'provenance': CP_PROVENANCE.name, 'provenance_sha256': digest(CP_PROVENANCE), 'reused_without_execution': True}

        fixtures = {}
        for name in ('call_ex_cross_activation_constructor', 'call_method_dict_cache_touch', 'inherited_call_ex_constructor', 'native_bound_zero_args', 'sqlite_native_aggregate', 'sqlite_cursor_completion', 'identity_last_use', 'hash_exception_preservation', 'sqlite_statement_cache', *S8_CASES):
            source = ROOT / 'tests/fixtures/core' / (name + '.py')
            expected_path = ROOT / 'tests/fixtures/expected' / (name + '.out')
            fixtures[name] = (source, normalized(expected_path.read_bytes()))
            track(source)
            track(expected_path)
        exact_source = ROOT / 'scratch/performance/exact-string-hash-fixture-proposed-20261008.py'
        exact_expected = '\n'.join(['exact text variants pass', 'str subclass callback preserved',
            'Python hash callbacks preserved', 'hash failures preserved', 'builtin rebinding preserved'])
        track(exact_source)
        guard_source = ROOT / 'scratch/performance/sqlite-cursor-writer-guard-probe-20261008.py'
        track(guard_source, '85320a8fb6a75e3793e43d8c912775647e4eed29bceb4ef8f43089dc2227c7bd')
        guard_expected = '\n'.join('cursor-guard-' + name for name in
            ('execute', 'fetchone', 'fetchall', 'next', 'close', 'executescript', 'executemany'))
        record['preserved_cpython3147_focused'] = [
            cp_reference(DATA / 'native-bound-zero-args-cpython3147-reference-20261008.json', *fixtures['native_bound_zero_args']),
            cp_reference(args.identity_reference.resolve(), *fixtures['identity_last_use'],
                         original_source=ROOT / 'scratch/performance/identity-last-use-r2-fixture-20261008.py'),
            cp_reference(DATA / 'hash-exception-preservation-cpython3147-reference-20261008.json', *fixtures['hash_exception_preservation']),
            cp_reference(DATA / 'exact-string-hash-cpython3147-reference-20261008.json', exact_source, exact_expected)]
        cache_reference_path = args.cache_reference.resolve(strict=True)
        cache_reference = json.loads(cache_reference_path.read_text(encoding='utf-8'))
        assert cache_reference.get('status') == 'terminal' and cache_reference.get('output_matches_expected'), 'Cache CP reference must be successful terminal evidence'
        assert cache_reference['expected_sha256'] == digest(ROOT / 'tests/fixtures/expected/sqlite_statement_cache.out'), 'CP cache expected output differs'
        record['preserved_cpython3147_focused'].append(
            cp_reference(cache_reference_path, *fixtures['sqlite_statement_cache']))
        record['required_cache_reference'] = {'record': cache_reference_path.name, 'sha256': digest(cache_reference_path),
            'source_sha256': cache_reference['source_sha256'], 'expected_sha256': cache_reference['expected_sha256'],
            'scope': 'Frozen cache fixture only; exact 3.14.7 receipt reused without execution'}
        # New references are byte-pinned strict CP fixtures, never re-executed.
        # Working-tree CRLF conversion is allowed; source/expected content is not.
        def required_fresh_reference(path, name, groups):
            path = path.resolve(strict=True)
            reference = json.loads(path.read_text(encoding='utf-8'))
            assert reference.get('status') == 'terminal' and reference['exit_code'] == 0
            assert reference.get('output_matches_expected') and reference.get('hashes_unchanged')
            assert not reference.get('timeout', False)
            assert reference.get('expected_groups', reference.get('groups_expected')) == groups
            assert Path(reference['cpython_executable']).resolve() == CPYTHON.resolve()
            assert reference['cpython_executable_sha256'] == record['cpython3147_binary_sha256']
            assert reference['cpython_dll_sha256'] == record['cpython3147_dll_sha256']
            original = Path(reference['source']).resolve(strict=True)
            frozen_expected = (Path(reference['expected']) if reference.get('expected')
                else original.parent.parent / 'expected' / (name + '.out')).resolve(strict=True)
            track(frozen_expected, reference['expected_sha256'])
            actual_expected = ROOT / 'tests/fixtures/expected' / (name + '.out')
            assert actual_expected.read_bytes().replace(b'\r\n', b'\n') == frozen_expected.read_bytes().replace(b'\r\n', b'\n')
            saved = cp_reference(path, *fixtures[name])
            saved.update(fixture=name, groups=groups, expected_sha256=reference['expected_sha256'],
                expected_source=str(frozen_expected), scope='Strict fixture only; normalized frozen source/expected bytes; CPython 3.14.7 reused without execution')
            record['preserved_cpython3147_focused'].append(saved)
            return saved

        record['required_dict_reference'] = required_fresh_reference(
            args.dict_reference, 'call_method_dict_cache_touch', 8)
        record['required_constructor_reference'] = required_fresh_reference(
            args.constructor_reference, 'inherited_call_ex_constructor', 9)
        record['required_cross_activation_reference'] = required_fresh_reference(
            args.cross_activation_reference, 'call_ex_cross_activation_constructor', 7)
        previous_path = DATA / 'sqlite-aggregate-r5-validation-20261008.json'
        track(previous_path)
        previous = json.loads(previous_path.read_text(encoding='utf-8'))
        cp_phase = next(row for row in previous['phases'] if row['name'] == 'cpython3147-focused')
        assert cp_phase['exit_code'] == 0
        aggregate_source, aggregate_expected = fixtures['sqlite_native_aggregate']
        assert digest(aggregate_source) == previous['source_sha256']['tests/fixtures/core/sqlite_native_aggregate.py']
        cp_log = DATA / cp_phase['log']
        track(cp_log, cp_phase['sha256'])
        assert normalized(cp_log.read_bytes()) == aggregate_expected
        cursor_ref_path = DATA / 'sqlite-cursor-r6-cpython3147-20261008.json'
        track(cursor_ref_path)
        cursor_ref = json.loads(cursor_ref_path.read_text(encoding='utf-8'))
        assert cursor_ref['exit_code'] == 0 and cursor_ref['groups'] == 8 and cursor_ref['output_matches_expected']
        cursor_original = ROOT / 'scratch/performance/sqlite-cursor-completion-fixture-r6-20261008.py'
        track(cursor_original, cursor_ref['source_sha256'])
        assert fixtures['sqlite_cursor_completion'][0].read_bytes().replace(b'\r\n', b'\n') == cursor_original.read_bytes().replace(b'\r\n', b'\n')
        cursor_log = DATA / 'sqlite-cursor-r6-cpython3147-20261008.log'
        track(cursor_log, cursor_ref['log_sha256'])
        assert normalized(cursor_log.read_bytes()) == fixtures['sqlite_cursor_completion'][1]
        record['preserved_cpython3147_focused'].extend([
            {'record': previous_path.name, 'sha256': digest(previous_path), 'phase': cp_phase, 'reused_without_execution': True},
            {'record': cursor_ref_path.name, 'sha256': digest(cursor_ref_path), 'reused_without_execution': True}])
        crash_reference = DATA / 'sqlite-cursor-writer-guard-isolated-cpython3147-20261008.json'
        track(crash_reference)
        record['preserved_cpython_guard_crash_reference'] = {'output': crash_reference.name, 'sha256': digest(crash_reference)}

        spec = importlib.util.spec_from_file_location('combined_fixture_counts', ROOT / 'tests/run_fixtures.py')
        suite = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(suite)
        required_cases = {'identity_last_use', 'hash_exception_preservation', 'sqlite_statement_cache',
            'call_ex_cross_activation_constructor', 'call_method_dict_cache_touch', 'inherited_call_ex_constructor',
            'sorted_key_scoped_entry', 'sorted_key_iteration_owner', 'sorted_key_nested_handled_context'}
        required_cases.update(C5_CASES); required_cases.update(S8_CASES)
        assert required_cases <= set(suite.CORE_CASES), 'Register all permanent fixtures before trial'
        assert all(suite.CORE_CASES.count(name) == 1 for name in required_cases)
        ps_text = (ROOT / 'tests/run_fixtures.ps1').read_text(encoding='utf-8')
        for name in ('call_ex_cross_activation_constructor', 'call_method_dict_cache_touch', 'inherited_call_ex_constructor',
            'sorted_key_scoped_entry', 'sorted_key_iteration_owner', 'sorted_key_nested_handled_context', *C5_CASES, *S8_CASES):
            assert len(re.findall(r'^\s*"' + re.escape(name) + r'",\s*$', ps_text, re.MULTILINE)) == 1
        cpp_text = (ROOT / 'tests/cpp/interpreter_tests.cpp').read_text(encoding='utf-8')
        assert cpp_text.count('#include "inherited_call_ex_constructor_cases.h"') == 1
        assert cpp_text.count('xlang3::test::check_inherited_call_ex_constructor(result);') == 1
        assert cpp_text.count('#include "scoped_sorted_key_cases.h"') == 1
        assert cpp_text.count('xlang3::test::check_scoped_sorted_key_cases(result);') == 1
        assert cpp_text.count('#include "ordinary_canonical_slot_constructor_cases.h"') == 1
        assert cpp_text.count('xlang3::test::check_ordinary_canonical_slot_constructor(result);') == 1
        assert cpp_text.count('#include "class_method_annotation_capture_cases.h"') == 1
        assert cpp_text.count('xlang3::test::check_class_method_annotation_capture(result);') == 1
        suite_text = (ROOT / 'tests/run_fixtures.py').read_text(encoding='utf-8')
        assert len(re.findall(r'^    assert_failure\(', suite_text, re.MULTILINE)) == 3
        record['fixture_counts'] = {'core': len(suite.CORE_CASES), 'compatibility_sections': len(suite.SECTION_CASES), 'expected_failures': 3}
        assert record['fixture_counts'] == {'core':398,'compatibility_sections':11,'expected_failures':3}
        for group, names in [('core', suite.CORE_CASES), ('compat_sections', suite.SECTION_CASES)]:
            for name in names:
                track(ROOT / 'tests/fixtures' / group / (name + '.py'))
                track(ROOT / 'tests/fixtures/expected' / ('' if group == 'core' else group) / (name + '.out'))
        for name in ('uncaught_exception', 'uncaught_runtime_error', 'unset_instance_attr'):
            track(ROOT / 'tests/fixtures/core' / (name + '.py'))
        for relative in ('tests/run_fixtures.py', 'tests/run_expected.py', 'benchmarks/check_regression.py',
                         'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py',
                         'benchmarks/diagnostics/preserve_pyperformance_partial.py'):
            track(ROOT / relative)
        gate_spec = importlib.util.spec_from_file_location('combined_gate_inputs', ROOT / 'benchmarks/check_regression.py')
        gate_module = importlib.util.module_from_spec(gate_spec)
        gate_spec.loader.exec_module(gate_module)
        assert len(gate_module.CASES) == 11
        record['fixed_gate_source_sha256'] = {name: track(ROOT / 'benchmarks/cases' / (name + '.py'))
                                              for name in gate_module.CASES}
        for name in ('old_xlang_sqlite_api', 'python_sqlite3_api'):
            track(ROOT / 'tests/native/sqlite' / (name + '.py'))
            track(ROOT / 'tests/native/sqlite' / (name + '.out'))
        record['expected_fixture_hashes'] = {name: {'source_sha256': digest(source),
            'expected_sha256': digest(ROOT / 'tests/fixtures/expected' / (name + '.out'))}
            for name, (source, _) in fixtures.items()}
        record['expected_fixture_hashes']['exact_string_hash'] = {'source_sha256': digest(exact_source), 'expected_stdout': exact_expected}
        record['expected_fixture_hashes']['sqlite_writer_guards'] = {'source_sha256': digest(guard_source), 'expected_stdout': guard_expected}
        record['hashes_before'] = dict(tracked)
        save()

        for name, (source, expected) in fixtures.items():
            phase('focused-' + name.replace('_', '-'), [CANDIDATE, source], expected=expected)
        phase('focused-exact-string-hash', [CANDIDATE, exact_source], expected=exact_expected)
        phase('focused-cursor-writer-guards', [CANDIDATE, guard_source], expected=guard_expected)
        phase('focused-threading-runtime-edges',[CANDIDATE,THREAD_SOURCE],expected=normalized(THREAD_EXPECTED.read_bytes()))
        regex = '^(' + '|'.join(sorted(CTEST_NAMES)) + ')$'
        common_ctest = [CTEST, '--test-dir', ROOT / 'build-repro/main-verify-20261006', '-C', 'Release', '-R', regex]
        _, discovery_log = phase('ctest-inventory', [*common_ctest, '--show-only=json-v1'])
        discovered = json.loads(discovery_log.read_text(encoding='utf-8'))['tests']
        assert {test['name'] for test in discovered} == CTEST_NAMES and len(discovered) == 9
        record['registered_ctest_names'] = sorted(CTEST_NAMES)
        _, cpp_log = phase('cpp-native-sqlite', [*common_ctest, '--output-on-failure'], timeout=300)
        assert '100% tests passed, 0 tests failed out of 9' in cpp_log.read_text(encoding='utf-8')
        for name in ('old_xlang_sqlite_api', 'python_sqlite3_api'):
            native = ROOT / 'tests/native/sqlite'
            phase('manual-' + name.replace('_', '-'), [CANDIDATE, native / (name + '.py'), CANDIDATE.parent / 'modules'],
                  expected=normalized((native / (name + '.out')).read_bytes()))
        phase('full-fixtures-core-compat-expected', [CPYTHON, ROOT / 'tests/run_fixtures.py', CANDIDATE], timeout=600)
        record['correctness_passed'] = True
        gate_path = DATA / (args.prefix + '-fixed-gate.json')
        phase('fixed-gate', [CPYTHON, ROOT / 'benchmarks/check_regression.py', '--baseline', BASELINE,
                            '--candidate', CANDIDATE, '--output', gate_path], timeout=900)
        gate = json.loads(gate_path.read_text(encoding='utf-8'))
        assert gate['status'] == 'pass' and len(gate['cases']) == 11
        assert (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .1)
        assert set(gate['cases']) == set(record['fixed_gate_source_sha256'])
        assert all(row['source_sha256'] == record['fixed_gate_source_sha256'][name]
                   for name, row in gate['cases'].items())
        record['fixed_gate'] = {'output': gate_path.name, 'sha256': digest(gate_path), 'exit_code': 0}
        env.update(PYTHONPATH=str(HOOK.parent), PYTHONIOENCODING='utf-8')
        official_ok = True
        for name in CASES:
            official = DATA / (args.prefix + '-official-' + name.replace('_', '-') + '-fast.json')
            row, _ = phase('official-' + name.replace('_', '-'), [CPYTHON,
                ROOT / 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py', '--runtime', CANDIDATE,
                '--benchmarks', name, '--mode', 'fast', '--case-timeout', '300', '--dependency-site', SITE,
                '--output', official], timeout=360, required=False)
            receipt = {'output': official.name, 'sha256': digest(official) if official.exists() else None,
                       'exit_code': row['exit_code'], 'complete': False}
            if row['passed'] and official.exists():
                try:
                    document = json.loads(official.read_text(encoding='utf-8'))
                    receipt['values_count'] = {subtest:len(values_for(document, subtest)) for subtest in OFFICIAL_SUBTESTS[name]}
                    receipt['pure_python_metadata']=pure_metadata(document)
                    receipt['complete'] = True
                except Exception as error:
                    receipt['validation_error'] = type(error).__name__ + ': ' + str(error)
            partial_dir = official.parent / (official.stem + '-partial')
            receipt['partial_evidence_sha256'] = {p.relative_to(DATA).as_posix():digest(p) for p in partial_dir.rglob('*') if p.is_file()} if partial_dir.is_dir() else {}
            record['official_pickle_pure_python'] = receipt
            official_ok &= receipt['complete']
            save()
        record['status'] = 'validated' if official_ok else 'correctness_and_gate_passed_official_failed'
        record['full_validated'] = official_ok
    except BaseException as error:
        record['error'] = type(error).__name__ + ': ' + str(error)
        if record['status'] == 'running': record['status'] = 'failed_validation'
        raise
    finally:
        after = {name: digest(name) if Path(name).is_file() else None for name in tracked}
        record['hashes_after'] = after
        record['hashes_unchanged'] = after == tracked
        if not record['hashes_unchanged']: record['status'] = 'failed_hash_integrity'
        current_release = {path.relative_to(ROOT).as_posix():digest(path) for path in CANDIDATE.parent.rglob('*') if path.is_file()}
        record['release_tree_unchanged'] = current_release == record.get('binaries_sha256')
        if not record['release_tree_unchanged']: record['status'] = 'failed_hash_integrity'
        if record['status'] != 'validated': record['full_validated'] = False
        record.update(terminal=True, completed_utc=datetime.now(timezone.utc).isoformat())
        record['terminal_record'] = {'status': record['status'], 'hashes_unchanged': record['hashes_unchanged'],
            'full_validated':record['full_validated'], 'release_tree_unchanged':record['release_tree_unchanged'],
            'controller_sha256': digest(Path(__file__)), 'source_inventory_sha256': digest(inventory_path)}
        save()
    print('Combined validation terminal:', record['status'], flush=True)
    return 0 if record['status'] == 'validated' else 1


if __name__ == '__main__':
    raise SystemExit(main())
