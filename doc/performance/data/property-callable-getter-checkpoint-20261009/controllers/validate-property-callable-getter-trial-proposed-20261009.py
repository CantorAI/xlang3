"""Root-only property correctness: fresh full checks, fixed gate, original X SQLAlchemy.

No parent correctness reuse. Saved CP SQL results remain historical and unpaired. Untimed checks log MSBuild and owned CTest
observations without accepting timing. Timed gate/official guards stay strict.
A same-source126 pending-correctness receipt alone may resume performance.
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
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BASELINE = ROOT / 'build-repro/Release'
PARENT = ROOT / 'build-repro/controls/property-callable-getter-parent-20261009'
PARENT_SHA = '608aadc303532321de8ba8cf464ce7d341ba6366373d386ab2230e865c9f6573'
WATCH = ROOT / 'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'
WATCH_SHA = '50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf'
RUNNER = ROOT / 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py'
RUNNER_SHA = 'c078ee77d5d66165f0bf4b2d782df56e71868ada827c81c5c53516c8d017ceaf'
SITE = ROOT / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'
CTEST = Path('C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe')
CTEST_NAMES = {'xlang3_sdk_stream_call_tests', 'xlang3_graph_producer', 'xlang3_graph_consumer',
    'xlang3_graph_reject_BAD_VERSION', 'xlang3_graph_reject_NO_CODEC', 'xlang3_graph_reject_DECODE_FAIL',
    'xlang3_runtime_value_tests', 'xlang3_interpreter_tests', 'xlang3_cli_sqlite_module_imports'}
TARGETS = {'src/executor/xlang_vm/ops/xlang_vm_ops_attr.h', 'tests/run_fixtures.py',
    'tests/run_fixtures.ps1', 'tests/fixtures/core/property_callable_getter.py',
    'tests/fixtures/expected/property_callable_getter.out'}
FOCUS_NAMES = ['cpp-interpreter', 'property_callable_getter', 'property_descriptor',
    'inline_property_dynamic_attr', 'descriptor_callable_keyword', 'object_getattribute_descriptor',
    'custom_getattribute_descriptor', 'recursive_property_error', 'nested_profile_setting', 'trace_hooks']
SAVED_CP_RECEIPT = DATA / 'triple-string-closing-comment-validation-20261008.json'
SAVED_CP_SHA = '05ffba4d1786c93448812db6ba22debc5f1c4be62ba6571ead209e49a234777a'
SAVED_CP_CONTROLLER = ROOT / 'scratch/performance/validate-triple-string-closing-comment-trial-r2-proposed-20261008.py'
SAVED_CP_CONTROLLER_SHA = 'f1d561d95a5ecea7efe9b4d4f4678e200c05d3ab9e8f019f945fc86314965ca5'
PROPERTY_CP = DATA / 'property-callable-getter-cpython-reference-20261009.json'
PROPERTY_CP_SHA = '586520b943a130092e9eff8ba88d5fdce1e82b79481227845a729c4ade711532'
BENCHMARKS = ('sqlalchemy_declarative', 'sqlalchemy_imperative')

class PerformancePending(Exception):
    pass

sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in sorted(root.rglob('*')) if p.is_file()}
normalized = lambda raw: raw.decode('utf-8').replace('\r\n', '\n').rstrip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source-inventory', 'focused-receipt', 'proposal-provenance', 'build-receipt'):
        parser.add_argument('--' + name, type=Path, required=True)
        parser.add_argument('--' + name + '-sha256', required=True)
    parser.add_argument('--resume-correctness-receipt', type=Path)
    parser.add_argument('--resume-correctness-receipt-sha256')
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    assert sys.implementation.name == 'cpython' and sys.version_info[:3] == (3, 14, 7) and not sys.flags.optimize
    assert Path(sys.executable).resolve() == CP.resolve() and Path.cwd().resolve() == ROOT
    assert re.fullmatch(r'[a-z0-9-]+', args.prefix) and not any(DATA.glob(args.prefix + '*'))
    output = DATA / (args.prefix + '.json')
    pins, protected = {}, {}
    record = dict(status='preflight', terminal=False, full_validated=False, whole_goal_complete=False,
        correctness_reused_same_candidate=False, correctness_passed=False, phases=[], idle_guards=[],
        fixed_gate=None, official_results={}, fresh_correctness=True,
        scope='Fresh source126 complete correctness/default11 gate and original X SQLAlchemy; historical exact-input CP3.14.7 reference, no parent correctness reuse, paired claim or full97 run',
        started_utc=datetime.now(timezone.utc).isoformat())

    def save():
        output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))

    def pin(path, expected=None):
        path = Path(path).resolve(strict=True)
        value = sha(path)
        assert expected is None or value == expected, str(path)
        assert str(path) not in pins or pins[str(path)] == value
        pins[str(path)] = value
        return value

    def stable():
        return (all(Path(p).is_file() and sha(p) == h for p, h in pins.items())
                and all(tree(Path(p)) == values for p, values in protected.items()))

    def idle(name, timed=False):
        raw = subprocess.check_output(['powershell', '-NoProfile', '-Command',
            'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'])
        rows = json.loads(raw.decode('utf-8-sig') or '[]')
        if isinstance(rows, dict):
            rows = [rows]
        tools = {'cl.exe', 'link.exe', 'ninja.exe', 'msbuild.exe', 'cmake.exe', 'ctest.exe',
                 'nmake.exe', 'lld-link.exe', 'clang-cl.exe', 'sample-pprint-native-cpu-20261008.exe'}
        observed = [r for r in rows if r['ProcessId'] != os.getpid() and
                    (r['Name'].lower() in tools or r['Name'].lower().startswith(('python', 'xlang3')))]
        logged = [] if timed else [r for r in observed if r['Name'].lower() == 'msbuild.exe']
        busy = observed if timed else [r for r in observed if r['Name'].lower() != 'msbuild.exe']
        record['idle_guards'].append(dict(phase=name, timed=timed, busy=busy,
            logged_untimed_msbuild=logged, timing_allowance=False))
        save()
        return busy

    def require_idle(name, timed=False):
        busy = idle(name, timed)
        assert not busy, busy

    def ctest_semantics(name, out, err):
        assert err.read_bytes() == b''
        if name == 'ctest-inventory':
            tests = read(out)['tests']
            assert len(tests) == 9 and {row['name'] for row in tests} == CTEST_NAMES
        else:
            transcript = out.read_text(encoding='utf-8')
            assert '100% tests passed, 0 tests failed out of 9' in transcript
            names = re.findall(r'Test\s+#\d+:\s+(\S+)\s+\.*\s*Passed', transcript)
            assert len(names) == 9 and set(names) == CTEST_NAMES

    environment = dict(os.environ, XLANG3_PYTHON_LIB=str(CP.parent / 'Lib'))
    for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE', 'PYTHONHOME'):
        environment.pop(name, None)
    assert not environment.get('XLANG3_VM_OPCODE_TIMING')

    def phase(name, command, timeout, expected=None, timed=False):
        require_idle('before-' + name, timed)
        assert stable()
        out = DATA / (args.prefix + '-' + name + '.stdout.log')
        err = DATA / (args.prefix + '-' + name + '.stderr.log')
        row = dict(name=name, command=list(map(str, command)), timeout_seconds=timeout,
                   passed=False, timeout=False, timed=timed, timing_accepted=False, stdout_log=out.name, stderr_log=err.name)
        record['phases'].append(row)
        save()
        child = None
        finish = watcher.start_timing_process_watch(args.prefix, name, row)
        try:
            with out.open('xb') as stdout, err.open('xb') as stderr:
                child = subprocess.Popen(row['command'], cwd=ROOT, env=environment, stdin=subprocess.DEVNULL,
                    stdout=stdout, stderr=stderr, creationflags=subprocess.CREATE_NO_WINDOW)
                row['pid'] = child.pid
                save()
                try:
                    row['exit_code'] = child.wait(timeout=timeout)
                except subprocess.TimeoutExpired:
                    row['timeout'] = True
            assert row.get('exit_code') == 0 and not row['timeout']
            if expected is not None:
                row['output_matches_expected'] = normalized(out.read_bytes()) == expected
                assert row['output_matches_expected'] and err.read_bytes() == b''
            if name in ('ctest-inventory', 'ctest'):
                ctest_semantics(name, out, err)
            row['passed'] = True
        except BaseException as error:
            row['error'] = type(error).__name__ + ': ' + str(error)
        finally:
            try:
                if child is not None and child.poll() is None:
                    cleanup = subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=15)
                    row['cleanup_exit_code'] = cleanup.returncode
                    row['cleanup_stdout'] = cleanup.stdout.decode('utf-8', errors='replace')
                    row['cleanup_stderr'] = cleanup.stderr.decode('utf-8', errors='replace')
                    if child.poll() is None:
                        child.kill()
                    child.wait(timeout=15)
                row['owned_child_cleanup_completed'] = True
            except BaseException as error:
                row.update(owned_child_cleanup_completed=False, cleanup_error=type(error).__name__ + ': ' + str(error), passed=False)
            finally:
                for stream, path in (('stdout', out), ('stderr', err)):
                    try:
                        row[stream + '_sha256'] = sha(path) if path.is_file() else None
                    except BaseException as error:
                        row[stream + '_sha256'] = None
                        row[stream + '_hash_error'] = type(error).__name__ + ': ' + str(error)
                        row['passed'] = False
                try:
                    row['measurement_valid'] = finish()
                except BaseException as error:
                    row.update(measurement_valid=False, watch_finish_error=type(error).__name__ + ': ' + str(error))
                if timed:
                    row['passed'] = row['passed'] and row['measurement_valid']
                    row['timing_accepted'] = row['passed']
                else:
                    watch = row.get('external_process_watch', {})
                    tolerated, rejected = [], []
                    for observation in watch.get('overlaps', []):
                        for process in observation['busy']:
                            own_ctest = (name in ('ctest-inventory', 'ctest') and
                                Path(row['command'][0]).resolve() == CTEST.resolve() and
                                process['Name'].lower() == 'ctest.exe' and process['ProcessId'] == row.get('pid'))
                            if process['Name'].lower() == 'msbuild.exe' or own_ctest:
                                tolerated.append(process)
                            else:
                                rejected.append(process)
                    semantic_watch = (bool(watch) and not watch.get('scanner_errors') and not rejected
                                      and not row.get('watch_finish_error'))
                    row['untimed_watch_classification'] = dict(semantic_valid=semantic_watch,
                        tolerated_observations=tolerated, foreign_observations=rejected,
                        raw_measurement_valid=row['measurement_valid'], timing_accepted=False,
                        scope='Only logged MSBuild and the owned CTest PID in untimed correctness; raw watcher flags retained')
                    row['semantic_passed'] = row['passed'] and semantic_watch
                    row['passed'] = row['semantic_passed']
                save()
        post_busy = idle('after-' + name, timed)
        row['post_idle_guard_passed'] = not post_busy
        row['post_hashes_stable'] = stable()
        if post_busy or not row['post_hashes_stable']:
            row['passed'] = False
            row['timing_accepted'] = False
            if not timed:
                row['semantic_passed'] = False
            row['post_guard_error'] = dict(busy=post_busy, hashes_stable=row['post_hashes_stable'])
        save()
        assert row['post_idle_guard_passed'], post_busy
        assert row['post_hashes_stable']
        assert row['passed'], name
        print(name, 'PASS', flush=True)
        return out

    def official_summary(path, benchmark_name):
        document = read(path)
        rows = [r for r in document['benchmarks'] if r.get('metadata', {}).get('name', document.get('metadata', {}).get('name')) == benchmark_name]
        assert len(rows) == 1
        metadata = dict(document.get('metadata', {}), **rows[0].get('metadata', {}))
        assert metadata['name'] == benchmark_name and metadata['unit'] == 'second'
        values = [v for run in rows[0]['runs'] for v in run.get('values', [])]
        assert len(values) == 20 and all(math.isfinite(v) and v > 0 for v in values)
        return dict(output=path.name, sha256=sha(path), values_count=20, mean_seconds=statistics.mean(values),
                    sample_sd_seconds=statistics.stdev(values), metadata=metadata)

    def saved_cpython_reference(parent):
        pin(SAVED_CP_RECEIPT, SAVED_CP_SHA)
        pin(SAVED_CP_CONTROLLER, SAVED_CP_CONTROLLER_SHA)
        old = read(SAVED_CP_RECEIPT)
        assert old['status'] == 'trial_validation_failed' and old['terminal']
        assert old['correctness_passed'] and old['hashes_unchanged'] and not old['full_validated']
        assert old['controller_sha256'] == SAVED_CP_CONTROLLER_SHA
        assert old['source_sha256'] == parent['source_snapshot_sha256']
        assert old['baseline_sha256'] == parent['fixed_baseline_sha256']
        old_release = {Path(path).relative_to(RELEASE.relative_to(ROOT)).as_posix(): value
                       for path, value in old['binaries_sha256'].items()}
        assert old_release == parent['files_sha256']
        assert old['hashes_before'] == old['hashes_after']
        semantic = [CP, CP.with_name('python314.dll'), RUNNER,
                    ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py']
        for benchmark in BENCHMARKS:
            directory = CP.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks' / ('bm_' + benchmark)
            semantic.extend((directory / 'run_benchmark.py', directory / 'pyproject.toml'))
        semantic += [path for library in ('sqlalchemy', 'pyperf')
                     for path in sorted((SITE / library).rglob('*.py'))]
        semantic += sorted(SITE.glob('*.dist-info/METADATA'))
        exact = {str(path.resolve()) for path in semantic}
        def in_variable_closure(path):
            return ((path.suffix == '.py' and any(path.is_relative_to((SITE / library).resolve())
                                                  for library in ('sqlalchemy', 'pyperf')))
                    or (path.name == 'METADATA' and path.parent.parent == SITE.resolve()
                        and path.parent.name.endswith('.dist-info')))
        old_variable = {str(Path(path).resolve()) for path in old['hashes_before']
                        if in_variable_closure(Path(path).resolve())}
        current_variable = {path for path in exact if in_variable_closure(Path(path))}
        assert old_variable == current_variable
        for path in semantic:
            pin(path, old['hashes_before'][str(path.resolve())])
        results = {}
        for benchmark in BENCHMARKS:
            name = 'official-cpython3147-' + benchmark
            row = next(row for row in old['phases'] if row['name'] == name)
            summary = old['official_results'][benchmark]['cpython3147']
            output = DATA / summary['output']
            command = [CP, RUNNER, '--runtime', CP, '--benchmarks', benchmark,
                       '--mode', 'fast', '--case-timeout', '300', '--dependency-site', SITE, '--output', output]
            assert row['command'] == list(map(str, command)) and row['timeout_seconds'] == 360
            assert row['passed'] and row['exit_code'] == 0 and row['timed'] and row['timing_accepted']
            assert row['measurement_valid'] and row['owned_child_cleanup_completed'] and not row['timeout']
            assert row['post_idle_guard_passed'] and row['post_hashes_stable']
            for stream in ('stdout', 'stderr'):
                pin(DATA / row[stream + '_log'], row[stream + '_sha256'])
            watch = row['external_process_watch']
            pin(DATA / watch['log'], watch['sha256'])
            assert watch['measurement_valid'] and not watch['overlaps'] and not watch['scanner_errors']
            pin(output, summary['sha256'])
            assert official_summary(output, benchmark) == summary
            results[benchmark] = dict(summary, execution='historical_same_input_cpython3147',
                executed_again=False, prior_receipt_sha256=SAVED_CP_SHA,
                scope='Saved completed 20 values; same recorded interpreter/benchmark/dependency identities; historical unpaired reference')
        record['historical_cpython_reference'] = dict(receipt=SAVED_CP_RECEIPT.relative_to(ROOT).as_posix(),
            receipt_sha256=SAVED_CP_SHA, executed_again=False, semantic_input_count=len(exact),
            original_results_preserved=True,
            coverage_limit='Recorded CP exe/DLL, benchmark .py/TOML, runner/hook, SQLAlchemy/pyperf .py and dependency METADATA; not a complete stdlib/native-extension/transitive-file inventory')
        return results

    try:
        save()
        require_idle('preflight', timed=False)
        for name in ('source_inventory', 'focused_receipt', 'proposal_provenance', 'build_receipt'):
            pin(getattr(args, name), getattr(args, name + '_sha256'))
        pin(PARENT / 'preserved-release-provenance.json', PARENT_SHA)
        pin(WATCH, WATCH_SHA)
        pin(CP, '4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9')
        pin(CP.with_name('python314.dll'), '0f9857ffdfe010fe6b99328d58c2e3c7472ce75f336bf9c2ad9bd5bca3bce700')
        app, focus, proposal, build, parent = map(read, (args.source_inventory, args.focused_receipt,
            args.proposal_provenance, args.build_receipt, PARENT / 'preserved-release-provenance.json'))
        assert parent['terminal'] and parent['correctness_passed'] and parent['fixed_gate_passed']
        assert not parent['full_validated'] and parent['validation_sha256'] == SAVED_CP_SHA
        assert (parent['source_count'], parent['file_count'], parent['object_count']) == (124, 178, 149)
        assert set(proposal['candidate_source_sha256']) == TARGETS
        assert proposal['raw_before_sha256'] == parent['source_snapshot_sha256']
        sources = dict(parent['source_snapshot_sha256'], **proposal['candidate_source_sha256'])
        assert len(sources) == 126 and app['terminal'] and app['source_sha256'] == sources
        assert app['source_count'] == 126
        assert app['proposal_sha256'] == args.proposal_provenance_sha256 and app['parent_manifest_sha256'] == PARENT_SHA
        pin(PROPERTY_CP, PROPERTY_CP_SHA)
        cp_fixture = read(PROPERTY_CP)
        assert cp_fixture['status'] == 'cpython_reference_passed' and cp_fixture['terminal'] and cp_fixture['passed']
        assert cp_fixture['exit_code'] == 0 and not cp_fixture['timed'] and cp_fixture['hashes_unchanged']
        assert cp_fixture['hashes_before'] == cp_fixture['hashes_after']
        assert Path(cp_fixture['command'][0]).resolve() == CP.resolve()
        assert cp_fixture['proposal_sha256'] == args.proposal_provenance_sha256
        assert cp_fixture['fixture_sha256'] == sources['tests/fixtures/core/property_callable_getter.py']
        assert cp_fixture['expected_sha256'] == sources['tests/fixtures/expected/property_callable_getter.out']
        pin(DATA / cp_fixture['stdout_log'], cp_fixture['stdout_sha256'])
        pin(DATA / cp_fixture['stderr_log'], cp_fixture['stderr_sha256'])
        assert normalized((DATA / cp_fixture['stdout_log']).read_bytes()) == normalized(
            (ROOT / 'tests/fixtures/expected/property_callable_getter.out').read_bytes())
        assert (DATA / cp_fixture['stderr_log']).read_bytes() == b''
        assert focus['terminal'] and focus['hashes_unchanged'] and focus['status'] == 'targeted_correctness_passed'
        assert focus['source_inventory_sha256'] == args.source_inventory_sha256 and focus['source_sha256'] == sources
        assert focus['build_receipt_sha256'] == args.build_receipt_sha256
        assert [row['name'] for row in focus['phases']] == FOCUS_NAMES
        for row in focus['phases']:
            assert row['passed'] and row['exit_code'] == 0
            for stream in ('stdout', 'stderr'):
                pin(DATA / row[stream + '_log'], row[stream + '_sha256'])
            if row['name'] != 'cpp-interpreter':
                expected = ROOT / 'tests/fixtures/expected' / (row['name'] + '.out')
                assert normalized((DATA / row['stdout_log']).read_bytes()) == normalized(expected.read_bytes())
                assert (DATA / row['stderr_log']).read_bytes() == b''
        assert build['terminal'] and build['status'] == 'build_passed' and build['actual_root_tool_exit_code'] == 0
        assert build['source_inventory_sha256'] == args.source_inventory_sha256 and build['source_sha256'] == sources
        assert Path(build['build_directory']).resolve() == RELEASE.parent.resolve() and build['configuration'] == 'Release'
        assert build['binaries_sha256'] == focus['binaries_sha256']
        assert build['candidate_binary_sha256'] == focus['candidate_binary_sha256']
        pin(ROOT / build['log'], build['log_sha256'])
        release = focus['binaries_sha256']
        assert len(release) == 178 and len(parent['fixed_baseline_sha256']) == 177
        assert build['baseline_sha256'] == parent['fixed_baseline_sha256']
        assert release['xlang3.exe'] == focus['candidate_binary_sha256']['exe']
        assert release['xlang3_runtime.dll'] == focus['candidate_binary_sha256']['dll']
        protected = {str(RELEASE): release, str(BASELINE): parent['fixed_baseline_sha256']}
        protected[str(PARENT)] = dict({'Release/' + p: h for p, h in parent['files_sha256'].items()},
            **{'source-snapshot/' + p: h for p, h in parent['source_snapshot_sha256'].items()},
            **{'native-objects/' + p: h for p, h in parent['object_snapshot_sha256'].items()},
            **{'translation-units/' + p: h for p, h in parent['translation_unit_snapshot_sha256'].items()},
            **{'build-metadata/' + p: h for p, h in parent['build_metadata_sha256'].items()},
            **{'preserved-release-provenance.json': PARENT_SHA})
        for directory, values in protected.items():
            assert tree(Path(directory)) == values
        for path, expected in sources.items():
            pin(ROOT / path, expected)
        spec = importlib.util.spec_from_file_location('property_trial_watch', WATCH)
        watcher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(watcher)
        pin(ROOT / 'tests/run_fixtures.py', sources['tests/run_fixtures.py'])
        spec = importlib.util.spec_from_file_location('property_full_fixture_inventory', ROOT / 'tests/run_fixtures.py')
        suite = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(suite)
        assert len(suite.CORE_CASES) == 400 and len(suite.SECTION_CASES) == 11
        assert len(set(suite.CORE_CASES)) == 400 and suite.CORE_CASES.count('property_callable_getter') == 1
        assert len(re.findall(r'^    assert_failure\(', (ROOT / 'tests/run_fixtures.py').read_text(encoding='utf-8'), re.MULTILINE)) == 3
        for group, names in (('core', suite.CORE_CASES), ('compat_sections', suite.SECTION_CASES)):
            for name in names:
                pin(ROOT / 'tests/fixtures' / group / (name + '.py'))
                pin(ROOT / 'tests/fixtures/expected' / ('' if group == 'core' else group) / (name + '.out'))
        for name in ('uncaught_exception', 'uncaught_runtime_error', 'unset_instance_attr'):
            pin(ROOT / 'tests/fixtures/core' / (name + '.py'))
        for name in ('old_xlang_sqlite_api', 'python_sqlite3_api'):
            pin(ROOT / 'tests/native/sqlite' / (name + '.py'))
            pin(ROOT / 'tests/native/sqlite' / (name + '.out'))
        gate_script = ROOT / 'benchmarks/check_regression.py'
        pin(gate_script)
        spec = importlib.util.spec_from_file_location('property_default_gate', gate_script)
        gate_module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(gate_module)
        assert len(gate_module.CASES) == 11
        gate_sources = {name: pin(ROOT / 'benchmarks/cases' / (name + '.py')) for name in gate_module.CASES}
        for benchmark_name in BENCHMARKS:
            benchmark = CP.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks' / ('bm_' + benchmark_name)
            pin(benchmark / 'run_benchmark.py')
            pin(benchmark / 'pyproject.toml')
        for library in ('sqlalchemy', 'pyperf'):
            for path in sorted((SITE / library).rglob('*.py')):
                pin(path)
        for path in sorted(SITE.glob('*.dist-info/METADATA')):
            pin(path)
        pin(SITE / 'sqlalchemy/sql/selectable.py', 'eac5df2ea20a1acbbe0d866bb09864f9e310e3718e676cd395274e17c60547f2')
        pin(ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py', '2ffc217fe20497156bfcb1cebc7fb87203f33c6d4d3dbb6ecbcbcf91ea4cb317')
        pin(RUNNER, RUNNER_SHA)
        pin(ROOT / 'benchmarks/diagnostics/preserve_pyperformance_partial.py')
        pin(CTEST)
        pin(RELEASE.parent / 'CTestTestfile.cmake')
        pin(RELEASE.parent / 'CMakeCache.txt')
        pin(__file__)
        saved_cp = saved_cpython_reference(parent)
        record.update(controller_sha256=sha(__file__), source_inventory_sha256=args.source_inventory_sha256,
            source_sha256=sources, source_count=126, focused_receipt_sha256=args.focused_receipt_sha256,
            proposal_provenance_sha256=args.proposal_provenance_sha256, build_receipt_sha256=args.build_receipt_sha256,
            parent_manifest_sha256=PARENT_SHA,
            binaries_sha256={RELEASE.relative_to(ROOT).as_posix() + '/' + p: h for p,h in release.items()},
            candidate_binary_sha256=focus['candidate_binary_sha256'], baseline_sha256=parent['fixed_baseline_sha256'],
            fixture_counts=dict(core=400, compatibility_sections=11, expected_failure_cases=3),
            ctest_count=9, native_api_checks=2, hashes_before=dict(pins),
            cpython_fixture_reference_sha256=PROPERTY_CP_SHA,
            source_identity_limit=app['source_identity_limit'])
        save()
        regex = '^(' + '|'.join(sorted(CTEST_NAMES)) + ')$'
        ctest = [CTEST, '--test-dir', ROOT / 'build-repro/main-verify-20261006', '-C', 'Release', '-R', regex]
        native = ROOT / 'tests/native/sqlite'
        correctness_commands = [
            ('full-fixtures-clean', [CP, ROOT / 'tests/run_fixtures.py', RELEASE / 'xlang3.exe'], 600, ''),
            ('ctest-inventory', [*ctest, '--show-only=json-v1'], 120, None),
            ('ctest', [*ctest, '--output-on-failure'], 300, None),
            *[(name, [RELEASE / 'xlang3.exe', native / (name + '.py'), RELEASE / 'modules'], 120,
               normalized((native / (name + '.out')).read_bytes())) for name in ('old_xlang_sqlite_api', 'python_sqlite3_api')],
        ]
        assert bool(args.resume_correctness_receipt) == bool(args.resume_correctness_receipt_sha256)
        if args.resume_correctness_receipt:
            prior_raw = Path(args.resume_correctness_receipt).read_bytes()
            assert hashlib.sha256(prior_raw).hexdigest() == args.resume_correctness_receipt_sha256
            prior = json.loads(prior_raw)
            assert prior['status'] == 'correctness_passed_performance_pending' and prior['terminal']
            assert prior['correctness_passed'] and prior['hashes_unchanged'] and not prior['full_validated']
            assert prior['controller_sha256'] == sha(__file__) and prior['source_sha256'] == sources
            assert prior['source_inventory_sha256'] == args.source_inventory_sha256
            assert prior['focused_receipt_sha256'] == args.focused_receipt_sha256
            assert prior['build_receipt_sha256'] == args.build_receipt_sha256
            assert prior['proposal_provenance_sha256'] == args.proposal_provenance_sha256
            assert prior['binaries_sha256'] == record['binaries_sha256'] and prior['baseline_sha256'] == record['baseline_sha256']
            assert prior['parent_manifest_sha256'] == PARENT_SHA and prior['fixed_gate'] is None
            assert prior['historical_cpython_reference']['receipt_sha256'] == SAVED_CP_SHA
            assert prior['cpython_fixture_reference_sha256'] == PROPERTY_CP_SHA
            assert prior['hashes_before'] == prior['hashes_after']
            assert not prior['official_results'] and not prior.get('official_attempts')
            assert len(prior['phases']) == len(correctness_commands) == 5
            for path, expected in prior['hashes_after'].items():
                pin(path, expected)
            for old, (name, command, timeout, expected) in zip(prior['phases'], correctness_commands):
                assert old['name'] == name and old['command'] == list(map(str, command))
                assert old['timeout_seconds'] == timeout and not old['timed'] and not old['timing_accepted']
                assert old['passed'] and old['semantic_passed'] and old['exit_code'] == 0
                assert not old['timeout'] and old['owned_child_cleanup_completed']
                assert old['post_idle_guard_passed'] and old['post_hashes_stable']
                out, err = DATA / old['stdout_log'], DATA / old['stderr_log']
                pin(out, old['stdout_sha256']); pin(err, old['stderr_sha256'])
                watch = old['external_process_watch']
                pin(DATA / watch['log'], watch['sha256'])
                rejected = []
                for observation in watch['overlaps']:
                    for process in observation['busy']:
                        own = name in ('ctest', 'ctest-inventory') and process['Name'].lower() == 'ctest.exe' and process['ProcessId'] == old['pid']
                        if process['Name'].lower() != 'msbuild.exe' and not own:
                            rejected.append(process)
                assert not rejected and not watch['scanner_errors']
                if expected is not None:
                    assert normalized(out.read_bytes()) == expected and err.read_bytes() == b''
                if name in ('ctest-inventory', 'ctest'):
                    ctest_semantics(name, out, err)
                inherited = dict(old, execution='historical_same_current126_untimed_correctness',
                    executed_again=False, prior_receipt_sha256=args.resume_correctness_receipt_sha256)
                record['phases'].append(inherited)
            pin(args.resume_correctness_receipt, args.resume_correctness_receipt_sha256)
            record.update(correctness_reused_same_candidate=True,
                historical_correctness_receipt_sha256=args.resume_correctness_receipt_sha256)
            record['hashes_before'] = dict(pins)
        else:
            for name, command, timeout, expected in correctness_commands:
                phase(name, command, timeout, expected=expected, timed=False)
        record['correctness_passed'] = True
        save()
        timing_busy = idle('before-performance', timed=True)
        if timing_busy:
            record['timing_blocked_by'] = timing_busy
            raise PerformancePending('Fresh same-candidate correctness passed; strict timing idle is busy')
        gate_path = DATA / (args.prefix + '-fixed-gate.json')
        try:
            phase('fixed-gate', [CP, gate_script, '--baseline', BASELINE / 'xlang3.exe',
                  '--candidate', RELEASE / 'xlang3.exe', '--output', gate_path], 900, timed=True)
        finally:
            gate_rows = [row for row in record['phases'] if row['name'] == 'fixed-gate']
            record['fixed_gate'] = dict(output=gate_path.name,
                sha256=sha(gate_path) if gate_path.is_file() else None,
                exit_code=gate_rows[-1].get('exit_code') if gate_rows else None)
            save()
        gate = read(gate_path)
        assert gate['status'] == 'pass' and (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .1)
        assert set(gate['cases']) == set(gate_sources)
        assert all(row['source_sha256'] == gate_sources[name] for name, row in gate['cases'].items())
        record['fixed_gate'] = dict(output=gate_path.name, sha256=sha(gate_path), exit_code=0)
        save()
        environment.update(PYTHONPATH=str(ROOT / 'benchmarks/diagnostics/pyperf_compat'), PYTHONIOENCODING='utf-8')
        failures = []
        for benchmark_name in BENCHMARKS:
            record['official_results'][benchmark_name] = {'cpython3147': saved_cp[benchmark_name]}
            for name, runtime in (('xlang3', RELEASE / 'xlang3.exe'),):
                phase_name = 'official-' + name + '-' + benchmark_name
                official = DATA / (args.prefix + '-' + phase_name + '-fast.json')
                try:
                    phase(phase_name, [CP, RUNNER, '--runtime', runtime, '--benchmarks', benchmark_name,
                        '--mode', 'fast', '--case-timeout', '300', '--dependency-site', SITE, '--output', official], 360, timed=True)
                    record['official_results'][benchmark_name][name] = official_summary(official, benchmark_name)
                except AssertionError as error:
                    row = next((row for row in reversed(record['phases']) if row['name'] == phase_name), None)
                    if (row is None or not row.get('measurement_valid') or not row.get('owned_child_cleanup_completed')
                            or not row.get('post_idle_guard_passed') or not row.get('post_hashes_stable') or not stable()):
                        raise
                    failures.append(dict(benchmark=benchmark_name, runtime=name, error=str(error)))
                finally:
                    record.setdefault('official_attempts', {})[phase_name] = dict(output=official.name,
                        sha256=sha(official) if official.is_file() else None,
                        complete=name in record['official_results'][benchmark_name])
                    partial = DATA / (official.stem + '-partial')
                    record.setdefault('partial_evidence_sha256', {}).update({p.relative_to(DATA).as_posix(): sha(p)
                        for p in partial.rglob('*') if p.is_file()} if partial.is_dir() else {})
                    save()
        record['official_failures'] = failures
        assert not failures, failures
        comparisons = {}
        for benchmark_name, values in record['official_results'].items():
            cp_time, x_time = values['cpython3147']['mean_seconds'], values['xlang3']['mean_seconds']
            comparisons[benchmark_name] = dict(speed_cpython_over_xlang3=cp_time/x_time,
                time_xlang3_over_cpython=x_time/cp_time,
                scope='Fresh original X fast-mode result versus saved identical-input CP3.14.7 result; historical unpaired comparison, warnings retained, no isolated speed attribution or full97 claim')
        record.update(status='trial_validated', full_validated=True, comparison=comparisons)
    except PerformancePending as error:
        record.update(status='correctness_passed_performance_pending', full_validated=False,
            performance_pending_reason=str(error))
    except BaseException as error:
        record.update(status='trial_validation_failed', error=type(error).__name__ + ': ' + str(error))
    finally:
        record.update(terminal=True, hashes_after={p: sha(p) if Path(p).is_file() else None for p in pins},
                      hashes_unchanged=stable(), completed_utc=datetime.now(timezone.utc).isoformat())
        if not record['hashes_unchanged']:
            record.update(status='invalid_hash_drift', full_validated=False)
        save()
    return 0 if record['status'] == 'trial_validated' else (2 if record['status'] == 'correctness_passed_performance_pending' else 1)


if __name__ == '__main__':
    raise SystemExit(main())
