"""Root-only Python-new R4 validation: fresh full correctness, fixed default gate,
then unchanged official unpickle_pure_python on CPython 3.14.7 and fixed XLang3.
Only an authenticated same-candidate performance-pending receipt may reuse
untimed correctness. Timed guards/watch/postguards remain strict. No full97,
component causality, paired comparison or speed acceptance is claimed.
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
import traceback

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BASELINE = ROOT / 'build-repro/Release'
PARENT = ROOT / 'build-repro/controls/lambda-eager-comprehension-capture-accepted-r5-20261009'
PARENT_SHA = '9271314856ede7f173396b2f39449a01d785f61a1579ce5f93c7786a2961dfad'
HEAD = '4b1cfefc80f0bbb9e1f60c09c46d83619ded0e17'
WATCH = ROOT / 'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'
WATCH_SHA = '50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf'
RUNNER = ROOT / 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py'
RUNNER_SHA = 'c078ee77d5d66165f0bf4b2d782df56e71868ada827c81c5c53516c8d017ceaf'
SITE = ROOT / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'
CTEST = Path('C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe')
CTEST_NAMES = {'xlang3_sdk_stream_call_tests', 'xlang3_graph_producer', 'xlang3_graph_consumer',
    'xlang3_graph_reject_BAD_VERSION', 'xlang3_graph_reject_NO_CODEC', 'xlang3_graph_reject_DECODE_FAIL',
    'xlang3_runtime_value_tests', 'xlang3_interpreter_tests', 'xlang3_cli_sqlite_module_imports'}
ENGINE_TARGETS = {'src/executor/xlang_vm/ops/xlang_vm_ops_call.h',
    'src/executor/xlang_vm/xlang_frame.h', 'src/executor/xlang_vm/xlang_vm_loop.cpp',
    'src/executor/xlang_vm/xlang_vm_op_rows.h'}
FIXTURES = {
    'python_new_vm_continuation': ('fixture', 'expected', 'cpython_reference', 8),
    'python_new_completion_finalizer': ('completion_finalizer_fixture',
        'completion_finalizer_expected', 'finalizer_cpython_reference', 1),
}
OWNED_TARGETS = ENGINE_TARGETS | {'tests/run_fixtures.py', 'tests/run_fixtures.ps1'} | {
    'tests/fixtures/' + group + '/' + name + suffix
    for name in FIXTURES for group, suffix in (('core', '.py'), ('expected', '.out'))}
FOCUS_NAMES = ('python_new_vm_continuation,python_new_completion_finalizer,call_ex_cross_activation_constructor,'
    'inherited_call_ex_constructor,ordinary_canonical_slot_constructor,call_ex_constructor_cache,'
    'explicit_slot_descriptor_fallback,synchronous_class_argument_lifetime,class_namespace_lifetime,'
    'slot_descriptor_owner,nested_profile_setting,debug_trace_profile_edges,trace_local_and_exception,'
    'sys_monitoring_all_events,generator_result_release,generator_resume_state_reuse,'
    'saved_frame_recursion_limit,module_class_constructor_frame_growth,call_default_positional_frame_fastpath').split(',')
BENCHMARKS = ('unpickle_pure_python',)
BENCHMARK_DIR = CP.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks/bm_pickle'

class PerformancePending(Exception):
    pass

sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in sorted(root.rglob('*')) if p.is_file()}
normalized = lambda raw: raw.decode('utf-8').replace('\r\n', '\n').rstrip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source-inventory', 'focused-receipt', 'proposal-provenance', 'build-receipt', 'focused-controller'):
        parser.add_argument('--' + name, type=Path, required=True)
        parser.add_argument('--' + name + '-sha256', required=True)
    parser.add_argument('--resume-correctness-receipt', type=Path)
    parser.add_argument('--resume-correctness-receipt-sha256')
    parser.add_argument('--correctness-only', action='store_true')
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    assert sys.implementation.name == 'cpython' and sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated and not sys.flags.optimize
    assert Path(sys.executable).resolve() == CP.resolve() and Path.cwd().resolve() == ROOT
    assert re.fullmatch(r'[a-z0-9-]+', args.prefix) and not any(DATA.glob(args.prefix + '*'))
    output = DATA / (args.prefix + '.json')
    pins, protected = {}, {}
    record = dict(status='preflight', terminal=False, full_validated=False, whole_goal_complete=False,
        correctness_reused_same_candidate=False, correctness_passed=False, phases=[], idle_guards=[],
        fixed_gate=None, official_results={}, fresh_correctness=True,
        scope='Fresh Python-new source132 complete correctness/default11 gate and unchanged official unpickle_pure_python on CP3.14.7 and X; unpaired fast diagnostics, no full97 or component speed claim',
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
        assert metadata['pickle_module'] == 'pickle' and metadata['pickle_protocol'] == '5'
        assert metadata['inner_loops'] == 20
        values = [v for run in rows[0]['runs'] for v in run.get('values', [])]
        assert len(values) == 20 and all(math.isfinite(v) and v > 0 for v in values)
        return dict(output=path.name, sha256=sha(path), values_count=20, mean_seconds=statistics.mean(values),
                    sample_sd_seconds=statistics.stdev(values), metadata=metadata)

    try:
        save()
        require_idle('preflight', timed=False)
        for name in ('source_inventory', 'focused_receipt', 'proposal_provenance', 'build_receipt', 'focused_controller'):
            pin(getattr(args, name), getattr(args, name + '_sha256'))
        pin(PARENT / 'preserved-release-provenance.json', PARENT_SHA)
        pin(WATCH, WATCH_SHA)
        pin(CP, '4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9')
        pin(CP.with_name('python314.dll'), '0f9857ffdfe010fe6b99328d58c2e3c7472ce75f336bf9c2ad9bd5bca3bce700')
        app, focus, proposal, build, parent = map(read, (args.source_inventory, args.focused_receipt,
            args.proposal_provenance, args.build_receipt, PARENT / 'preserved-release-provenance.json'))
        assert parent['terminal'] and parent['full_validated'] and parent['correctness_passed'] and parent['fixed_gate_passed']
        assert (parent['source_count'], parent['file_count'], parent['object_count']) == (128, 178, 149)
        assert parent['validation_sha256'] == '6a41ee3fe92453d6472f2bb1c24b2da7ef125ac8425d27efdffa9f2262ab2d35'
        assert proposal['head'] == app['head'] == HEAD
        assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == HEAD
        assert not subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT)
        assert proposal['source_sha256'] == proposal['raw_before_sha256'] == parent['source_snapshot_sha256']
        assert len(proposal['source_sha256']) == proposal['source_count'] == 128
        assert proposal['parent_manifest_sha256'] == app['parent_manifest_sha256'] == PARENT_SHA
        assert set(proposal['candidate_source_sha256']) == ENGINE_TARGETS
        assert proposal['input_sha256'] == {p: parent['source_snapshot_sha256'][p] for p in ENGINE_TARGETS}
        assert set(app['targets_sha256']) == OWNED_TARGETS and len(OWNED_TARGETS) == 10
        sources = dict(parent['source_snapshot_sha256'], **app['targets_sha256'])
        assert app['terminal'] and app['status'] == 'applied_python_new_r4_tuple_api_correction'
        assert app['source_sha256'] == sources and app['source_count'] == len(sources) == 132
        assert app['proposal_sha256'] == args.proposal_provenance_sha256
        assert app['patch_sha256'] == proposal['patch_sha256']
        assert app['parent_release_preserved'] and app['fixed_baseline_unchanged']
        pin(ROOT / proposal['patch'], proposal['patch_sha256'])
        candidate_map = {row['target']: ROOT / row['source'] for row in proposal['mapping']}
        assert set(candidate_map) == ENGINE_TARGETS
        for name, path in candidate_map.items():
            pin(path, proposal['candidate_source_sha256'][name])
        pin(DATA / 'python-new-vm-continuation-r3-applied-source-20261009.json', app['r3_application_sha256'])
        original_app = read(DATA / 'python-new-vm-continuation-r3-applied-source-20261009.json')
        assert original_app['terminal'] and original_app['proposal_sha256'] == args.proposal_provenance_sha256
        assert original_app['targets_sha256'].keys() == app['targets_sha256'].keys()
        assert original_app['source_sha256'] == dict(parent['source_snapshot_sha256'], **original_app['targets_sha256'])
        assert all(original_app['targets_sha256'][p] == h for p, h in proposal['candidate_source_sha256'].items())
        pin(ROOT / app['correction_patch'], app['correction_patch_sha256'])
        assert app['correction_patch_sha256'] == '987b1bba11d6a32ca03f6ff9583a38205b61a1aec76d80796b3dd12210e57db5'
        rejected = DATA / 'python-new-vm-continuation-r3-build-20261009.json'
        pin(rejected, app['rejected_build_sha256'])
        assert read(rejected)['terminal'] and not read(rejected)['passed']
        changes = {
            'src/executor/xlang_vm/ops/xlang_vm_ops_call.h': (b'tuple->items.data() + 1', b'tuple->items.begin() + 1'),
            'src/executor/xlang_vm/xlang_vm_loop.cpp': (b'context->items.data() + 2', b'context->items.begin() + 2'),
        }
        for name in OWNED_TARGETS:
            if name in changes:
                before, after = changes[name]
                raw = candidate_map[name].read_bytes()
                assert raw.count(before) == 1
                corrected = raw.replace(before, after)
                assert hashlib.sha256(corrected).hexdigest() == app['targets_sha256'][name]
            else:
                assert app['targets_sha256'][name] == original_app['targets_sha256'][name]
        cp_fixture_receipts = {}
        for case, (fixture_key, expected_key, receipt_key, groups) in FIXTURES.items():
            original_fixture, original_expected = ROOT / proposal[fixture_key], ROOT / proposal[expected_key]
            pin(original_fixture, proposal[fixture_key + '_sha256'])
            pin(original_expected, proposal[expected_key + '_sha256'])
            assert sources['tests/fixtures/core/' + case + '.py'] == sha(original_fixture)
            assert sources['tests/fixtures/expected/' + case + '.out'] == sha(original_expected)
            receipt_path = ROOT / app[receipt_key]
            pin(receipt_path, app[receipt_key + '_sha256'])
            cp_fixture = read(receipt_path)
            assert cp_fixture['terminal'] and cp_fixture['passed'] and cp_fixture['exit_code'] == 0
            assert cp_fixture['hashes_unchanged'] and cp_fixture['hashes_before'] == cp_fixture['hashes_after']
            assert cp_fixture['version_info'] == [3, 14, 7] and not cp_fixture['timed']
            assert cp_fixture['command'] == [str(CP), '-I', str(original_fixture)]
            assert cp_fixture['expected_groups'] == cp_fixture['actual_stdout_groups'] == groups
            # The reference ran against the preserved pre-application Release.
            # Authenticate those identities through the immutable control, not
            # by pretending its historical binary hashes are current binaries.
            for path, h in cp_fixture['hashes_before'].items():
                path = Path(path).resolve()
                if path.is_relative_to(RELEASE.resolve()):
                    rel = path.relative_to(RELEASE.resolve()).as_posix()
                    assert parent['files_sha256'][rel] == h
                    pin(PARENT / 'Release' / rel, h)
                else:
                    pin(path, h)
            out, err = DATA / cp_fixture['stdout_log'], DATA / cp_fixture['stderr_log']
            pin(out, cp_fixture['stdout_sha256']); pin(err, cp_fixture['stderr_sha256'])
            assert normalized(out.read_bytes()) == normalized(original_expected.read_bytes())
            assert err.read_bytes() == b''
            cp_fixture_receipts[case] = app[receipt_key + '_sha256']
        focus_names = FOCUS_NAMES
        assert focus['terminal'] and focus['hashes_unchanged'] and focus['passed'] and focus['status'] == 'focused_passed'
        assert focus['application_sha256'] == args.source_inventory_sha256 and focus['source_sha256'] == sources
        assert focus['build_sha256'] == args.build_receipt_sha256 and focus['controller_sha256'] == args.focused_controller_sha256
        assert focus['expected_names'] == [row['name'] for row in focus['phases']] == focus_names
        assert focus['hashes_before'] == focus['hashes_after'] and not focus['timed']
        for path, h in focus['hashes_before'].items():
            pin(path, h)
        for row in focus['phases']:
            assert row['passed'] and row['exit_code'] == 0
            for stream in ('stdout', 'stderr'):
                pin(DATA / row[stream], row[stream + '_sha256'])
            fixture = ROOT / 'tests/fixtures/core' / (row['name'] + '.py')
            expected = ROOT / 'tests/fixtures/expected' / (row['name'] + '.out')
            assert [Path(p).resolve() for p in row['command']] == [(RELEASE / 'xlang3.exe').resolve(), fixture.resolve()]
            def focus_text(raw):
                return normalized(raw).replace(str(ROOT / 'tests'), 'tests').replace('tests\\fixtures\\core\\', 'tests/fixtures/core/')
            assert focus_text((DATA / row['stdout']).read_bytes()) == focus_text(expected.read_bytes())
            assert (DATA / row['stderr']).read_bytes() == b''
        assert build['terminal'] and build['passed'] and build['status'] == 'build_passed' and build['exit_code'] == 0
        assert build['application_sha256'] == args.source_inventory_sha256 and build['source_sha256'] == sources
        assert build['source_count'] == 132 and build['binary_file_count'] == 178
        assert build['source_unchanged'] and build['accepted_control_unchanged'] and build['fixed_baseline_unchanged']
        assert build['hashes_before'] == build['hashes_after'] and not build['timed']
        for path, h in build['hashes_before'].items():
            pin(path, h)
        assert build['binaries_sha256'] == focus['binaries_sha256']
        pin(DATA / build['log'], build['log_sha256'])
        candidate_binaries = {'exe': build['exe_sha256'], 'dll': build['dll_sha256']}
        # Accept either explicitly Release-relative or repo-relative map keys,
        # normalize exactly once, and require every file to lie in fixed Release.
        release = {}
        for name, h in focus['binaries_sha256'].items():
            path = (ROOT / name if name.startswith(RELEASE.relative_to(ROOT).as_posix() + '/') else RELEASE / name).resolve()
            rel = path.relative_to(RELEASE.resolve()).as_posix()
            assert rel not in release
            release[rel] = h
        assert len(release) == 178 and len(parent['fixed_baseline_sha256']) == 177
        assert release['xlang3.exe'] == candidate_binaries['exe']
        assert release['xlang3_runtime.dll'] == candidate_binaries['dll']
        assert release['xlang3_runtime.dll'] != parent['files_sha256']['xlang3_runtime.dll']
        protected = {str(RELEASE): release, str(BASELINE): parent['fixed_baseline_sha256']}
        protected[str(PARENT)] = dict({'Release/' + p: h for p, h in parent['files_sha256'].items()},
            **{'source-snapshot/' + p: h for p, h in parent['source_snapshot_sha256'].items()},
            **{'native-objects/' + p: h for p, h in parent['object_snapshot_sha256'].items()},
            **{'translation-units/' + p: h for p, h in parent['translation_unit_snapshot_sha256'].items()},
            **{'build-metadata/' + p: h for p, h in parent['build_metadata_sha256'].items()},
            **{'emitted-ir/' + p: h for p, h in parent.get('emitted_ir_sha256', {}).items()},
            **{'preserved-release-provenance.json': PARENT_SHA})
        for directory, values in protected.items():
            assert tree(Path(directory)) == values
        for path, expected in (sources | app['unowned_tracked_dirty_sha256']).items():
            pin(ROOT / path, expected)
        spec = importlib.util.spec_from_file_location('python_new_trial_watch', WATCH)
        watcher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(watcher)
        pin(ROOT / 'tests/run_fixtures.py', sources['tests/run_fixtures.py'])
        spec = importlib.util.spec_from_file_location('python_new_full_fixture_inventory', ROOT / 'tests/run_fixtures.py')
        suite = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(suite)
        assert len(suite.CORE_CASES) == len(set(suite.CORE_CASES)) == 403 and len(suite.SECTION_CASES) == 11
        assert all(suite.CORE_CASES.count(name) == 1 for name in FIXTURES)
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
        spec = importlib.util.spec_from_file_location('python_new_default_gate', gate_script)
        gate_module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(gate_module)
        assert len(gate_module.CASES) == 11
        gate_sources = {name: pin(ROOT / 'benchmarks/cases' / (name + '.py')) for name in gate_module.CASES}
        pin(BENCHMARK_DIR / 'run_benchmark.py')
        pin(BENCHMARK_DIR / 'pyproject.toml')
        definition = BENCHMARK_DIR / 'bm_unpickle_pure_python.toml'
        pin(definition)
        import tomllib
        options = tomllib.loads(definition.read_text(encoding='utf-8'))['tool']['pyperformance']
        assert options['name'] == 'unpickle_pure_python' and options['extra_opts'] == ['--pure-python', 'unpickle']
        for path in sorted((SITE / 'pyperf').rglob('*.py')):
            pin(path)
        for path in sorted(SITE.glob('*.dist-info/METADATA')):
            pin(path)
        for name in ('pickle.py', '_pydatetime.py', 'datetime.py', 'random.py', 'copyreg.py', 'struct.py', 'io.py', 'tomllib/__init__.py', 'tomllib/_parser.py'):
            pin(CP.parent / 'Lib' / name)
        pin(ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py', '2ffc217fe20497156bfcb1cebc7fb87203f33c6d4d3dbb6ecbcbcf91ea4cb317')
        pin(RUNNER, RUNNER_SHA)
        pin(ROOT / 'benchmarks/diagnostics/preserve_pyperformance_partial.py')
        pin(CTEST)
        pin(RELEASE.parent / 'CTestTestfile.cmake')
        pin(RELEASE.parent / 'CMakeCache.txt')
        pin(__file__)
        record.update(controller_sha256=sha(__file__), source_inventory_sha256=args.source_inventory_sha256,
            source_sha256=sources, source_count=len(sources), focused_receipt_sha256=args.focused_receipt_sha256,
            focused_controller_sha256=args.focused_controller_sha256, focused_phase_names=focus_names,
            proposal_provenance_sha256=args.proposal_provenance_sha256, build_receipt_sha256=args.build_receipt_sha256,
            parent_manifest_sha256=PARENT_SHA,
            binaries_sha256={RELEASE.relative_to(ROOT).as_posix() + '/' + p: h for p,h in release.items()},
            candidate_binary_sha256=candidate_binaries, baseline_sha256=parent['fixed_baseline_sha256'],
            fixture_counts=dict(core=403, compatibility_sections=11, expected_failure_cases=3),
            ctest_count=9, native_api_checks=2, hashes_before=dict(pins),
            cpython_fixture_reference_sha256=cp_fixture_receipts,
            source_identity_limit=app['source_identity_limit'],
            dependency_identity_limit='Recorded official source/TOML, runner/hook, pyperf .py, distribution METADATA and listed stdlib files; not a complete transitive stdlib/native-extension inventory')
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
            assert prior['cpython_fixture_reference_sha256'] == cp_fixture_receipts
            assert prior['focused_controller_sha256'] == args.focused_controller_sha256
            assert prior['focused_phase_names'] == FOCUS_NAMES
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
                inherited = dict(old, execution='historical_same_current132_untimed_correctness',
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
        if args.correctness_only:
            raise PerformancePending('Root requested untimed correctness only; no timed child attempted')
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
            record['official_results'][benchmark_name] = {}
            for name, runtime in (('cpython3147', CP), ('xlang3', RELEASE / 'xlang3.exe')):
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
                scope='Fresh unchanged official unpickle_pure_python fast-mode samples from CP3.14.7 and fixed XLang3; sequential unpaired comparison, warnings retained, no component causality or full97 claim')
        record.update(status='trial_validated', full_validated=True, comparison=comparisons)
    except PerformancePending as error:
        record.update(status='correctness_passed_performance_pending', full_validated=False,
            performance_pending_reason=str(error))
    except BaseException as error:
        record.update(status='trial_validation_failed', error=type(error).__name__ + ': ' + str(error),
                      error_traceback=traceback.format_exc())
    finally:
        record.update(terminal=True, hashes_after={p: sha(p) if Path(p).is_file() else None for p in pins},
                      hashes_unchanged=stable(), completed_utc=datetime.now(timezone.utc).isoformat())
        if not record['hashes_unchanged']:
            record.update(status='invalid_hash_drift', full_validated=False)
        save()
    return 0 if record['status'] == 'trial_validated' else (2 if record['status'] == 'correctness_passed_performance_pending' else 1)


if __name__ == '__main__':
    raise SystemExit(main())
