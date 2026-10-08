"""Fresh frame-locals retirement full correctness, fixed gate and original pure-Python pickle.

Root-only exact CPython3.14.7 controller. Require caller-pinned current111 source,
fresh ten-phase focused proof and the actual successful build command/raw log.
All full correctness phases are executed fresh, including three strict frame
fixtures. Historical S8 is reference/preservation evidence only. No correctness
phase is reused, and no build or CP reference recapture is launched.
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
PARENT_CONTROLLER = ROOT / 'scratch/performance/validate-sorted-exact-int-s8-full-20261008.py'
PARENT_CONTROLLER_SHA = '997d0e694f5c04aa2459bfbaca36065054c53d1661f810e37c5f7dd7b50813f3'
S8_VALIDATION = DATA / 'sorted-exact-int-s8-full-validation-20261008.json'
S8_VALIDATION_SHA = 'd4b028b1255f23b11e664bd9e6778b808a7d160d3628e17f39710faa1900d022'
APPLICATION = DATA / 'frame-locals-retirement-s8-application-20261008-applied-source.json'
APPLICATION_SHA = 'dc563e2b363dc3992a2c8b323e0f59e953749a715bb4ec97264ef1fa846ab153'
PROOF = ROOT / 'scratch/performance/frame-locals-retirement-r4-proposed-20261008-provenance.json'
PROOF_SHA = '24b1c58347f3bea0c4bc40d5e450efb04c8ff614f4c0b482cc12511380cf8e76'
PRESERVED = ROOT / 'build-repro/controls/frame-locals-retirement-s8-application-20261008'
PRESERVED_SHA = '29841491e3a6e934db9ef19dfdd01ef3ef71ab14e2cc3333b80213d793ab9e26'
BUILD_TERMINAL = DATA / 'frame-locals-retirement-r4-build-terminal-20261008.json'
BUILD_TERMINAL_SHA = 'df8ba2f7b324b6b29173e7622f47fbaa6a8c9ea86560e24db2557b1e8c93ae0a'
FRAME_REFERENCES = (
    ('runtime-frame-context-cpython3147-s8-correctness-r2-20261008.json',
     '159f7294d3ff4e1395722145e3bc8ef72a6ed47834989798cd588af0b5af7d41', 1),
    ('frame-locals-retained-cpython3147-s8-reference-20261008.json',
     '71a0288e97ffb357de8e447a7a9fca38450fb173201b708988d74341d7150751', 2))
FRAME_FIXTURES = (
    ('framecontext4', 'scratch/performance/runtime-frame-context-coalesced-fixture-proposed-20261008.py',
     'scratch/performance/runtime-frame-context-coalesced-expected-proposed-20261008.out',
     'fd942dfb3981c01b6464f623ebb1b37640772c532b18ad7381abebf2d9e5ffc7',
     '97d5a6c2764deb48e4f217f09d148f13b4f8332d88b6f930f14ba12ede9e065d'),
    ('retained1', 'scratch/performance/frame-locals-retained-mapping-fixture-proposed-20261008.py',
     'scratch/performance/frame-locals-retained-mapping-expected-proposed-20261008.out',
     '9cf97667d8ef24562c857f651bfd8d5489281e6fd3887f888995a63f256839df',
     '3498e5609c21461d63e16be4273c358cac309f47c59918e0632b95c2c7914150'),
    ('extra1', 'scratch/performance/frame-locals-retained-extra-fixture-proposed-20261008.py',
     'scratch/performance/frame-locals-retained-extra-expected-proposed-20261008.out',
     'ec47ae1e050c1c367125466834185d5cd9edb0b002ad9b03bac1213ae7686a88',
     'bcc83f86c18cc6060a085ea6736038efcb31a42bf2830f71fdb4f64e6eec3e71'))
THREAD_SOURCE = ROOT / 'tests/fixtures/core/threading_runtime_edges.py'
THREAD_SOURCE_SHA = '56016bd84f76782ce736058670c50035c8a1600ac67fca201ef1a304e6875c39'
THREAD_EXPECTED = ROOT / 'tests/fixtures/expected/threading_runtime_edges.out'
THREAD_EXPECTED_SHA = '7ac4a64a76bc6207024ea9c001dcfcf336e991effe39b9c08602ceb7c332ba22'
S8_CASES = ('call_module_global_binding', 'sorted_exact_integer_keys')
PURE_DEFINITION_SHA = '846f31a4f830d4b2ab044917d3b3e6b036ace3647445d770c8980f1f5b158c21'
FOCUSED_PHASES = ['cpp', 'framecontext4', 'retained1', 'extra1', 'debug_frames', 'profile3', 'threading', 'monitoring', 'sorted7', 'nested1']
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
    parser.add_argument('--focused-controller', required=True, type=Path)
    parser.add_argument('--focused-controller-sha256', required=True)
    parser.add_argument('--build-log', type=Path, required=True)
    parser.add_argument('--build-log-sha256', required=True)
    parser.add_argument('--prefix', required=True)
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
    assert sys.implementation.name == "cpython" and sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CPYTHON.resolve()
    assert sys.flags.optimize == 0 and ROOT == Path("D:/CantorAI/xlang3").resolve() and Path.cwd().resolve() == ROOT
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
    assert digest(inventory_path) == args.source_inventory_sha256 == APPLICATION_SHA and len(sources) == 111
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
    required_sources.add('tests/cpp/frame_locals_retirement_cases.h')
    assert required_sources <= sources.keys(), 'Supply the complete FINAL compiled source inventory, including both generic repairs and unchanged CPP counter dependency'
    record = {'status': 'running', 'terminal': False, 'started_utc': datetime.now(timezone.utc).isoformat(),
        'scope': 'Fresh frame-retirement full correctness/default fixed11 gate/original pure-Python pickle20; exact same-candidate targeted proof is prerequisite only, every full phase fresh; no old correctness reuse or speedup prerequisite',
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

    try:
        save()
        track(inventory_path, record['source_inventory_sha256'])
        track(Path(__file__))
        track(REFERENCE, REFERENCE_SHA)
        track(WATCH, WATCH_SHA)
        watcher_spec = importlib.util.spec_from_file_location('frame_retirement_full_timing_watch', WATCH)
        watcher = importlib.util.module_from_spec(watcher_spec)
        watcher_spec.loader.exec_module(watcher)
        track(PARENT_CONTROLLER, PARENT_CONTROLLER_SHA)
        track(S8_VALIDATION, S8_VALIDATION_SHA)
        parent_validation = json.loads(S8_VALIDATION.read_bytes())
        assert parent_validation['terminal'] and parent_validation['status'] == 'validated'
        assert parent_validation['full_validated'] and parent_validation['hashes_unchanged'] and parent_validation['release_tree_unchanged']
        assert parent_validation['terminal_record']['controller_sha256'] == PARENT_CONTROLLER_SHA
        assert parent_validation['fixture_counts'] == dict(core=398, compatibility_sections=11, expected_failures=3)
        track(PROOF, PROOF_SHA); proof = json.loads(PROOF.read_bytes())
        track(BUILD_TERMINAL, BUILD_TERMINAL_SHA); build = json.loads(BUILD_TERMINAL.read_bytes())
        assert build['terminal'] and build['status'] == 'build_completed' and build['exit_code'] == 0
        assert build['application_receipt_sha256'] == APPLICATION_SHA and (ROOT / build['application_receipt']).resolve() == inventory_path
        build_path = args.build_log.resolve(strict=True)
        assert build_path == (ROOT / build['build_log']).resolve()
        assert args.build_log_sha256 == build['build_log_sha256']
        track(build_path, args.build_log_sha256); track(ROOT / build['wrapper'], build['wrapper_sha256'])
        assert build['command'] == ['cmd.exe', '/d', '/c', 'scratch\\performance\\build-frame-locals-retirement-r4-20261008.cmd']
        assert inventory['terminal'] and inventory['status'] == 'applied_unbuilt_unvalidated_frame_locals_retirement_correctness'
        assert inventory['proposal_sha256'] == PROOF_SHA and inventory['source_count'] == 111
        assert not inventory['accepted_gate_baseline_changed'] and inventory['baseline_before'] == inventory['baseline_after']
        assert sources == dict(parent_validation['source_sha256'], **proof['candidate_source_sha256'])
        assert inventory['owned_targets'] == proof['candidate_file_list'] and len(proof['candidate_source_sha256']) == 3
        for path, sha in proof['candidate_source_sha256'].items(): track(ROOT / proof['candidate_root'] / path, sha)
        for path, sha in parent_validation['source_sha256'].items(): track(ROOT / proof['raw_input_root'] / path, sha)
        manifest_path = PRESERVED / 'preserved-release-provenance.json'
        track(manifest_path, PRESERVED_SHA); preserved = json.loads(manifest_path.read_bytes())
        assert inventory['preserved_manifest_sha256'] == PRESERVED_SHA and Path(inventory['preserved_parent']).resolve() == PRESERVED.resolve()
        assert preserved['source_snapshot_sha256'] == parent_validation['source_sha256'] and preserved['source_count'] == 110
        assert preserved['file_count'] == len(preserved['files_sha256']) == 178
        assert preserved['files_sha256'] == {Path(path).relative_to(CANDIDATE.parent.relative_to(ROOT)).as_posix(): sha for path, sha in parent_validation['binaries_sha256'].items()}
        assert preserved['baseline_sha256'] == inventory['baseline_after'] and len(preserved['baseline_sha256']) == 177
        baseline = {path.relative_to(BASELINE.parent).as_posix(): digest(path) for path in BASELINE.parent.rglob('*') if path.is_file()}
        assert baseline == preserved['baseline_sha256']
        record['baseline_sha256'] = baseline
        for path, sha in baseline.items(): track(BASELINE.parent / path, sha)
        for directory, values in ((PRESERVED, preserved['files_sha256']), (PRESERVED / 'source-snapshot', preserved['source_snapshot_sha256']),
            (PRESERVED / 'unrelated-dirty-snapshot', preserved['unrelated_dirty_snapshot_sha256'])):
            for path, sha in values.items(): track(directory / path, sha)
        preserved_tree_before = {path.relative_to(PRESERVED).as_posix(): digest(path) for path in PRESERVED.rglob('*') if path.is_file()}
        record['preserved_build_log'] = dict(path=str(build_path), sha256=args.build_log_sha256, exit_code=0,
            terminal_record=BUILD_TERMINAL.name, terminal_record_sha256=BUILD_TERMINAL_SHA, actual_command=build['command'], build_launched=False)
        record['historical_s8_reference'] = dict(record=S8_VALIDATION.name, sha256=S8_VALIDATION_SHA, correctness_reused=False)
        for name, sha in sources.items(): track(ROOT / name, sha)
        # Pin binaries freshly after the parent's build, including all own native modules.
        binary_paths = sorted(path for path in CANDIDATE.parent.rglob('*') if path.is_file())
        assert len(binary_paths) == 178
        assert CANDIDATE in binary_paths and CANDIDATE.with_name('xlang3_runtime.dll') in binary_paths
        record['binaries_sha256'] = {path.relative_to(ROOT).as_posix(): track(path) for path in binary_paths}
        record['candidate_binary_sha256'] = {'exe': digest(CANDIDATE), 'dll': digest(CANDIDATE.with_name('xlang3_runtime.dll'))}
        focused_controller = args.focused_controller.resolve(strict=True)
        assert focused_controller == (ROOT / 'scratch/performance/check-frame-locals-retirement-r4-focused-proposed-20261008.py').resolve()
        assert args.focused_controller_sha256 == 'f6c352a403312cd97b9370e04390394ae9745df17b36f078fd169d7e961abc42'
        track(focused_controller, args.focused_controller_sha256)
        focused_path = args.focused_receipt.resolve(strict=True)
        track(focused_path, args.focused_receipt_sha256)
        focused = json.loads(focused_path.read_text(encoding='utf-8'))
        assert focused['terminal'] and focused['status'] == 'targeted_correctness_passed' and focused['hashes_unchanged']
        assert focused['controller_sha256'] == args.focused_controller_sha256 and focused['application_receipt_sha256'] == APPLICATION_SHA
        assert focused['source_sha256'] == sources and focused['source_inventory_sha256'] == digest(inventory_path)
        assert focused['baseline_sha256'] == baseline and focused['root_recorded_build_exit_code'] == 0
        assert focused['build_receipt_sha256'] == BUILD_TERMINAL_SHA and focused['build_wrapper_sha256'] == build['wrapper_sha256']
        assert focused['build_log_sha256'] == args.build_log_sha256 and Path(focused['build_log']).resolve() == build_path
        assert focused['build_argv'] == build['command']
        assert focused['build_argv_sha256'] == hashlib.sha256(json.dumps(build['command'], separators=(',', ':')).encode('utf-8')).hexdigest()
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
        assert focused['hashes_before'] == focused['hashes_after']
        for path, sha in focused['hashes_before'].items(): track(Path(path), sha)
        track(THREAD_SOURCE, THREAD_SOURCE_SHA); track(THREAD_EXPECTED, THREAD_EXPECTED_SHA)
        references = []
        for filename, sha, cp_count in FRAME_REFERENCES:
            path = DATA / filename; track(path, sha); reference = json.loads(path.read_bytes())
            assert reference['terminal'] and reference['hashes_unchanged']
            cp_rows = [row for row in reference['phases'] if row['runtime'] == 'cpython3147']
            assert len(cp_rows) == cp_count and all(row['passed'] and row['exit_code'] == 0 and not row['timeout'] and row['output_matches_expected'] for row in cp_rows)
            for row in reference['phases']:
                for stream in ('stdout', 'stderr'): track(DATA / row[stream + '_log'], row[stream + '_sha256'])
            references.append(reference)
        assert references[0]['source_sha256'] == FRAME_FIXTURES[0][3] and references[0]['expected_sha256'] == FRAME_FIXTURES[0][4]
        for row, fixture in zip(references[1]['fixtures'], FRAME_FIXTURES[1:]):
            assert row['source_sha256'] == fixture[3] and row['expected_sha256'] == fixture[4]
        for _, source, expected, source_sha, expected_sha in FRAME_FIXTURES:
            track(ROOT / source, source_sha); track(ROOT / expected, expected_sha)
        track(BASELINE, 'a5f5028c15e145edce645a5afc25c11fbce77f51e882312b1fbe06e63c72a4af')
        track(BASELINE.with_name('xlang3_runtime.dll'), 'bc1b9c0a8086f7e6fb0c037516dc9c1eea20427fa887e3aa623714bc5ef5da8d')
        cp_provenance = json.loads(CP_PROVENANCE.read_text(encoding='utf-8'))
        assert cp_provenance['status'] == 'finished' and cp_provenance['runtime_version'] == '3.14.7'
        record['cpython3147_binary_sha256'] = track(CPYTHON, cp_provenance['sha256_start']['exe'])
        record['cpython3147_dll_sha256'] = track(CPYTHON.with_name('python314.dll'), cp_provenance['sha256_start']['dll'])
        record['compatibility_hook_sha256'] = track(HOOK, cp_provenance['compatibility_hook_sha256'])
        track(CP_PROVENANCE, parent_validation['hashes_before'][str(CP_PROVENANCE.resolve())])
        track(CP_FULL, parent_validation['hashes_before'][str(CP_FULL.resolve())])
        cp_document = json.loads(CP_FULL.read_text(encoding='utf-8'))
        benchmark_root = CPYTHON.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks'
        record['benchmark_source_sha256'] = {}
        definition=benchmark_root/'bm_pickle/bm_pickle_pure_python.toml'
        track(definition,PURE_DEFINITION_SHA)
        original_benchmark = benchmark_root / 'bm_pickle/run_benchmark.py'
        track(original_benchmark, '31c0e30be79514b45db0d4e858632eb429b0fea3edbac6463ad5f34d92f3cab8')
        assert "sys.modules['_pickle'] = None" in original_benchmark.read_text(encoding='utf-8')
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
            'preserved_cp_metadata':pure_metadata(cp_document), 'blocked_pickle': 'Pinned original --pure-python branch sets sys.modules[_pickle]=None and rejects accelerated pickle', 'scope':'Original pure-Python definition/protocol; saved CP Oct7 comparison is unpaired'}
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
        assert cpp_text.count('#include "frame_locals_retirement_cases.h"') == 1
        assert cpp_text.count('xlang3::test::check_frame_locals_retirement(result);') == 1
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

        for name, source, expected, _, _ in FRAME_FIXTURES:
            phase('focused-' + name, [CANDIDATE, ROOT / source], expected=normalized((ROOT / expected).read_bytes()))
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
        assert all(Path(path).is_file() and digest(path) == sha for path, sha in tracked.items())
        assert {path.relative_to(CANDIDATE.parent).as_posix(): digest(path) for path in CANDIDATE.parent.rglob('*') if path.is_file()} == {Path(path).relative_to(CANDIDATE.parent.relative_to(ROOT)).as_posix(): sha for path, sha in record['binaries_sha256'].items()}
        assert {path.relative_to(BASELINE.parent).as_posix(): digest(path) for path in BASELINE.parent.rglob('*') if path.is_file()} == baseline
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
        record['baseline_tree_unchanged'] = {path.relative_to(BASELINE.parent).as_posix(): digest(path) for path in BASELINE.parent.rglob('*') if path.is_file()} == record.get('baseline_sha256')
        record['preserved_parent_tree_unchanged'] = {path.relative_to(PRESERVED).as_posix(): digest(path) for path in PRESERVED.rglob('*') if path.is_file()} == locals().get('preserved_tree_before')
        if not record['baseline_tree_unchanged'] or not record['preserved_parent_tree_unchanged']: record['status'] = 'failed_hash_integrity'
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
