"""Prepared serial combined validation; author must not execute this controller.

Use exact CPython 3.14.7 with --source-inventory FINAL_APPLIED_SOURCE.json.
Accepts source_sha256 {path: sha} or files {path: {working_sha256: sha}}.
The parent owns application/build/runtime. No build or CP baseline is launched.
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
CASES = {'sqlite_synth': 'bm_sqlite_synth', 'sqlglot_v2_parse': 'bm_sqlglot_v2'}
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
    parser.add_argument('--prefix', default='interpreter-lazy-globals-validation-20261008')
    parser.add_argument('--identity-reference', type=Path,
        default=DATA / 'identity-last-use-r2-cpython3147-reference-20261008.json')
    args = parser.parse_args()
    assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CPYTHON.resolve()
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
    required_sources = {'modules/sqlite/sqlite_package.cpp', 'modules/sqlite/sqlite_values.cpp',
        'modules/sqlite/sqlite_handles.h', 'sdk/xlang3/abi/xmodule.h',
        'src/import/native_package_loader.cpp', 'src/builtins/functional_builtins.cpp',
        'src/runtime/value_hash.cpp', 'tests/cpp/interpreter_tests.cpp'}
    assert required_sources <= sources.keys(), 'Supply the complete FINAL combined source inventory'
    record = {'status': 'running', 'terminal': False, 'started_utc': datetime.now(timezone.utc).isoformat(),
        'scope': 'Isolated lazy per-Interpreter fallback globals trial; existing accepted correctness and two official cases',
        'source_inventory': str(inventory_path), 'source_inventory_sha256': digest(inventory_path),
        'source_sha256': sources, 'phases': [], 'idle_guards': [],
        'known_limits': ['SQLite registration factory GC edges are not published',
            'Pre-existing scalar registration failure ownership gap is separate',
            'Conservative cursor lookahead guard does not claim unrestricted CPython reentrancy parity',
            'CPython guard diagnostic crashes are retained; no blanket guard parity claim'],
        'official_score_scope': 'Original sqlite_synth and sqlglot_v2_parse only; no full-suite win claim'}
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
        command = 'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'
        completed = subprocess.run(['powershell', '-NoProfile', '-Command', command], capture_output=True, check=True)
        rows = json.loads(completed.stdout.decode('utf-8-sig') or '[]')
        if isinstance(rows, dict): rows = [rows]
        tool_names = {'cl.exe', 'link.exe', 'ninja.exe', 'msbuild.exe', 'cmake.exe', 'ctest.exe',
                      'nmake.exe', 'lld-link.exe', 'clang-cl.exe'}
        busy = [row for row in rows if row['ProcessId'] != os.getpid() and
                (row['Name'].lower() in tool_names or row['Name'].lower().startswith(('python', 'xlang3')))]
        record['idle_guards'].append({'phase': name, 'allowed_controller_pid': os.getpid(), 'busy': busy})
        save()
        assert not busy, busy

    env = os.environ.copy()
    env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
    for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
        env.pop(name, None)
    assert not env.get('XLANG3_VM_OPCODE_TIMING'), 'Official validation must be uninstrumented'

    def phase(name, command, timeout=120, expected=None, required=True):
        idle(name)
        paths = [DATA / (args.prefix + '-' + name + suffix) for suffix in ('.stdout.log', '.stderr.log')]
        assert not any(path.exists() for path in paths)
        print('Starting', name, flush=True)
        row = {'name': name, 'command': list(map(str, command)), 'timeout_seconds': timeout}
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
        row.update(stdout_log=paths[0].name, stderr_log=paths[1].name,
                   stdout_sha256=digest(paths[0]), stderr_sha256=digest(paths[1]))
        row.update(log=paths[0].name, sha256=row['stdout_sha256'])
        if expected is not None:
            row['output_matches_expected'] = normalized(paths[0].read_bytes()) == expected
        row['passed'] = row['exit_code'] == 0 and row.get('output_matches_expected', True)
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
        for name, sha in sources.items(): track(ROOT / name, sha)
        # Pin binaries freshly after the parent's build, including all own native modules.
        binary_paths = sorted(path for path in CANDIDATE.parent.rglob('*') if path.suffix.lower() in ('.exe', '.dll'))
        assert CANDIDATE in binary_paths and CANDIDATE.with_name('xlang3_runtime.dll') in binary_paths
        record['binaries_sha256'] = {path.relative_to(ROOT).as_posix(): track(path) for path in binary_paths}
        record['candidate_binary_sha256'] = {'exe': digest(CANDIDATE), 'dll': digest(CANDIDATE.with_name('xlang3_runtime.dll'))}
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
        for name, directory in CASES.items():
            source = benchmark_root / directory / 'run_benchmark.py'
            expected_sha = cp_provenance['benchmark_python_sources'][directory + '\\run_benchmark.py']
            record['benchmark_source_sha256'][name] = track(source, expected_sha)
            values_for(cp_document, name)
        record['preserved_official_cpython3147'] = {'output': CP_FULL.name, 'sha256': digest(CP_FULL),
            'provenance': CP_PROVENANCE.name, 'provenance_sha256': digest(CP_PROVENANCE), 'reused_without_execution': True}

        fixtures = {}
        for name in ('native_bound_zero_args', 'sqlite_native_aggregate', 'sqlite_cursor_completion', 'identity_last_use', 'hash_exception_preservation'):
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
        assert {'identity_last_use', 'hash_exception_preservation'} <= set(suite.CORE_CASES), 'Register both permanent fixtures first'
        suite_text = (ROOT / 'tests/run_fixtures.py').read_text(encoding='utf-8')
        assert len(re.findall(r'^    assert_failure\(', suite_text, re.MULTILINE)) == 3
        record['fixture_counts'] = {'core': len(suite.CORE_CASES), 'compatibility_sections': len(suite.SECTION_CASES), 'expected_failures': 3}
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
                    receipt['values_count'] = len(values_for(json.loads(official.read_text(encoding='utf-8')), name))
                    receipt['complete'] = True
                except Exception as error:
                    receipt['validation_error'] = type(error).__name__ + ': ' + str(error)
            record['official_sqlite_synth' if name == 'sqlite_synth' else 'official_sqlglot_parse'] = receipt
            official_ok &= receipt['complete']
            save()
        record['status'] = 'validated' if official_ok else 'correctness_and_gate_passed_official_failed'
    except BaseException as error:
        record['error'] = type(error).__name__ + ': ' + str(error)
        if record['status'] == 'running': record['status'] = 'failed_validation'
        raise
    finally:
        after = {name: digest(name) if Path(name).is_file() else None for name in tracked}
        record['hashes_after'] = after
        record['hashes_unchanged'] = after == tracked
        if not record['hashes_unchanged']: record['status'] = 'failed_hash_integrity'
        record.update(terminal=True, completed_utc=datetime.now(timezone.utc).isoformat())
        record['terminal_record'] = {'status': record['status'], 'hashes_unchanged': record['hashes_unchanged'],
            'controller_sha256': digest(Path(__file__)), 'source_inventory_sha256': digest(inventory_path)}
        save()
    print('Combined validation terminal:', record['status'], flush=True)
    return 0 if record['status'] == 'validated' else 1


if __name__ == '__main__':
    raise SystemExit(main())
