"""Root-only UTF-8 trial: fresh full correctness, default11 gate, official pickle.

Authenticate the exact terminal R1 observer false-positive and reuse only its
three completed untimed correctness phases. Original negative flags/raw are
retained; API2, default gate21/5/.10 and official pickle20 execute fresh.
No timing exception is introduced; no full97 or automatic acceptance.
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
PARENT = ROOT / 'build-repro/controls/native-str-utf8-encode-parent-20261008'
PARENT_SHA = 'cd5035f531a513104f2948286e81d4996bb01a9b6dc4c84353303f370de4bd97'
PAIRED_CONTROLLER = ROOT / 'scratch/performance/measure-native-str-utf8-original-pickle-paired-proposed-20261008.py'
WATCH = ROOT / 'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'
WATCH_SHA = '50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf'
PRIOR_CONTROLLER = ROOT / 'scratch/performance/validate-native-str-utf8-original-pickle-trial-proposed-20261008.py'
PRIOR_CONTROLLER_SHA = 'e02a8aa15973a1b153348807f7ca4b0c4adb0bcbd3f3a42042f69d7da0f46133'
PRIOR_RECEIPT_SHA = 'bd123e16ce4355fdffa7e430cf4ec191af280ced17adc3f14ee59bbc7019b83a'
RESUME_NAMES = ('full-fixtures-clean', 'ctest-inventory', 'ctest')
RUNNER = ROOT / 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py'
RUNNER_SHA = 'c078ee77d5d66165f0bf4b2d782df56e71868ada827c81c5c53516c8d017ceaf'
SITE = ROOT / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'
CTEST = Path('C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe')
CTEST_NAMES = {'xlang3_sdk_stream_call_tests', 'xlang3_graph_producer', 'xlang3_graph_consumer',
    'xlang3_graph_reject_BAD_VERSION', 'xlang3_graph_reject_NO_CODEC', 'xlang3_graph_reject_DECODE_FAIL',
    'xlang3_runtime_value_tests', 'xlang3_interpreter_tests', 'xlang3_cli_sqlite_module_imports'}
TARGETS = {'src/runtime/methods/string_methods.cpp', 'src/runtime/modules/system/codecs_module.cpp',
    'src/internal/xlang3/builtins.h', 'tests/run_fixtures.py', 'tests/run_fixtures.ps1',
    'tests/fixtures/core/native_str_utf8_encode.py', 'tests/fixtures/expected/native_str_utf8_encode.out'}
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in sorted(root.rglob('*')) if p.is_file()}
normalized = lambda raw: raw.decode('utf-8').replace('\r\n', '\n').rstrip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source-inventory', 'focused-receipt', 'proposal-provenance', 'build-receipt', 'paired-receipt'):
        parser.add_argument('--' + name, type=Path, required=True)
        parser.add_argument('--' + name + '-sha256', required=True)
    parser.add_argument('--paired-controller-sha256', required=True)
    parser.add_argument('--resume-receipt', type=Path, required=True)
    parser.add_argument('--resume-receipt-sha256', required=True)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    assert sys.implementation.name == 'cpython' and sys.version_info[:3] == (3, 14, 7) and not sys.flags.optimize
    assert Path(sys.executable).resolve() == CP.resolve() and Path.cwd().resolve() == ROOT
    assert re.fullmatch(r'[a-z0-9-]+', args.prefix) and not any(DATA.glob(args.prefix + '*'))
    output = DATA / (args.prefix + '.json')
    pins, protected = {}, {}
    resume_rows, consumed_resume = {}, set()
    record = dict(status='preflight', terminal=False, full_validated=False, whole_goal_complete=False,
        correctness_reused_same_candidate=True, correctness_passed=False, phases=[], idle_guards=[],
        fixed_gate=None, official_results={}, fresh_correctness=False, fresh_remaining_correctness=True,
        scope='Exact historical untimed fixture/CTest correctness retained; API2/default11 gate and original official pickle on X/CP3.14.7 fresh; R1 watcher failure is not rewritten',
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

    def idle(name):
        raw = subprocess.check_output(['powershell', '-NoProfile', '-Command',
            'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'])
        rows = json.loads(raw.decode('utf-8-sig') or '[]')
        if isinstance(rows, dict):
            rows = [rows]
        tools = {'cl.exe', 'link.exe', 'ninja.exe', 'msbuild.exe', 'cmake.exe', 'ctest.exe',
                 'nmake.exe', 'lld-link.exe', 'clang-cl.exe', 'sample-pprint-native-cpu-20261008.exe'}
        busy = [r for r in rows if r['ProcessId'] != os.getpid() and
                (r['Name'].lower() in tools or r['Name'].lower().startswith(('python', 'xlang3')))]
        record['idle_guards'].append(dict(phase=name, busy=busy))
        save()
        assert not busy, busy

    environment = dict(os.environ, XLANG3_PYTHON_LIB=str(CP.parent / 'Lib'))
    for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE', 'PYTHONHOME'):
        environment.pop(name, None)
    assert not environment.get('XLANG3_VM_OPCODE_TIMING')

    def authenticate_resume():
        assert args.resume_receipt_sha256 == PRIOR_RECEIPT_SHA
        pin(args.resume_receipt, PRIOR_RECEIPT_SHA)
        pin(PRIOR_CONTROLLER, PRIOR_CONTROLLER_SHA)
        old = read(args.resume_receipt)
        assert old['terminal'] and old['status'] == 'trial_validation_failed' and old['error'] == 'AssertionError: ctest'
        assert old['hashes_unchanged'] and old['hashes_before'] == old['hashes_after']
        assert not old['full_validated'] and not old['correctness_passed']
        assert old['fixed_gate'] is None and old['official_results'] == {}
        assert [r['name'] for r in old['phases']] == list(RESUME_NAMES)
        for field in ('source_inventory_sha256', 'source_sha256', 'source_count', 'focused_receipt_sha256',
                      'paired_receipt_sha256', 'proposal_provenance_sha256', 'build_receipt_sha256',
                      'parent_manifest_sha256', 'binaries_sha256', 'baseline_sha256', 'fixture_counts',
                      'ctest_count', 'native_api_checks'):
            assert old[field] == record[field], field
        assert old['hashes_after'][str(PRIOR_CONTROLLER.resolve())] == PRIOR_CONTROLLER_SHA
        for path, expected in old['hashes_after'].items():
            pin(path, expected)
        regex = '^(' + '|'.join(sorted(CTEST_NAMES)) + ')$'
        ctest = list(map(str, [CTEST, '--test-dir', ROOT / 'build-repro/main-verify-20261006', '-C', 'Release', '-R', regex]))
        commands = {'full-fixtures-clean': list(map(str, [CP, ROOT / 'tests/run_fixtures.py', RELEASE / 'xlang3.exe'])),
                    'ctest-inventory': [*ctest, '--show-only=json-v1'], 'ctest': [*ctest, '--output-on-failure']}
        timeouts = {'full-fixtures-clean': 600, 'ctest-inventory': 120, 'ctest': 300}
        for row in old['phases']:
            name = row['name']
            assert row['command'] == commands[name] and row['timeout_seconds'] == timeouts[name]
            assert row['exit_code'] == 0 and not row['timeout'] and row['owned_child_cleanup_completed']
            assert isinstance(row['pid'], int) and row['pid'] > 0
            for stream in ('stdout', 'stderr'):
                pin(DATA / row[stream + '_log'], row[stream + '_sha256'])
            assert (DATA / row['stderr_log']).read_bytes() == b''
            watch = row['external_process_watch']
            pin(DATA / watch['log'], watch['sha256'])
            assert watch['poll_interval_seconds'] == 1 and watch['scanner_errors'] == []
            observations = [json.loads(line) for line in (DATA / watch['log']).read_text(encoding='utf-8').splitlines() if line.strip()]
            assert observations and all('error' not in observation for observation in observations)
            observed_overlaps = [observation for observation in observations if observation['busy']]
            # Retained receipt timestamps use a different offset spelling; compare
            # instants and exact Name/PID rows, without rewriting either raw file.
            def identity(observation):
                return (datetime.fromisoformat(observation['observed_utc']).astimezone(timezone.utc), observation['busy'])
            assert [identity(o) for o in observed_overlaps] == [identity(o) for o in watch['overlaps']]
            if name == 'ctest':
                assert row['passed'] is False and row['measurement_valid'] is False and watch['measurement_valid'] is False
                assert observed_overlaps and watch['sha256'] == '5d568eb38275bd0d9f11ce59379a1730486c9ea5d3ccdc06a23474b097ffc8a2'
                assert row['pid'] == 35388
                for observation in observed_overlaps:
                    assert observation['busy'] == [{'Name': 'ctest.exe', 'ProcessId': row['pid']}]
                stdout = (DATA / row['stdout_log']).read_text(encoding='utf-8')
                passed = re.findall(r'^\s*\d+/9 Test\s+#\d+:\s+(\S+)\s+\.+\s+Passed\s+[0-9.]+\s+sec\s*$', stdout, re.MULTILINE)
                assert len(passed) == 9 and set(passed) == CTEST_NAMES
                assert '100% tests passed, 0 tests failed out of 9' in stdout
            else:
                assert row['passed'] and row['measurement_valid'] and watch['measurement_valid']
                assert not observed_overlaps
                if name == 'full-fixtures-clean':
                    assert row['output_matches_expected'] and (DATA / row['stdout_log']).read_bytes() == b''
            resume_rows[name] = row
        inventory = read(DATA / resume_rows['ctest-inventory']['stdout_log'])
        assert len(inventory['tests']) == 9 and {test['name'] for test in inventory['tests']} == CTEST_NAMES
        for test in inventory['tests']:
            assert test['command']
            for argument in test['command']:
                path = Path(argument)
                if path.suffix.lower() == '.exe' and path.is_absolute() and path.parent.resolve() == RELEASE.resolve():
                    relative = path.relative_to(ROOT).as_posix()
                    assert relative in record['binaries_sha256']
                    pin(path, record['binaries_sha256'][relative])
        record['historical_failed_receipt'] = dict(path=str(args.resume_receipt.resolve()), sha256=PRIOR_RECEIPT_SHA,
            status=old['status'], correctness_passed=old['correctness_passed'], full_validated=False,
            phases=old['phases'], original_file_unchanged=True)
        record['untimed_ctest_classification'] = dict(semantic_passed=True, owned_pid=35388,
            original_passed=False, original_measurement_valid=False,
            original_watch_sha256=resume_rows['ctest']['external_process_watch']['sha256'],
            reason='The sole observer overlap is the exact owned CTest process; exit0/all nine Passed/empty stderr. No performance timing is accepted from this phase.',
            timed_phase_watch_exception=False)
        record['resume_authenticated'] = True
        record['hashes_before'] = dict(pins)
        assert stable()
        save()

    def phase(name, command, timeout, expected=None):
        idle('before-' + name)
        assert stable()
        if name in RESUME_NAMES:
            assert record.get('resume_authenticated') and name in resume_rows and name not in consumed_resume
            original = resume_rows[name]
            assert original['command'] == list(map(str, command)) and original['timeout_seconds'] == timeout
            out = DATA / original['stdout_log']
            if expected is not None:
                assert normalized(out.read_bytes()) == expected
            row = json.loads(json.dumps(original))
            # Preserve original passed/measurement_valid and raw watcher flags.
            # semantic_passed is a separate, narrowly authenticated untimed fact.
            row.update(execution='historical_same_candidate_untimed_correctness', executed_again=False,
                semantic_passed=True, timing_accepted=False, prior_receipt_sha256=PRIOR_RECEIPT_SHA,
                owner_observer_false_positive=(name == 'ctest'))
            record['phases'].append(row)
            consumed_resume.add(name)
            record['reused_phase_names'] = [value for value in RESUME_NAMES if value in consumed_resume]
            save()
            return out
        out = DATA / (args.prefix + '-' + name + '.stdout.log')
        err = DATA / (args.prefix + '-' + name + '.stderr.log')
        row = dict(name=name, command=list(map(str, command)), timeout_seconds=timeout,
                   passed=False, timeout=False, stdout_log=out.name, stderr_log=err.name)
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
                row['passed'] = row['passed'] and row['measurement_valid']
                save()
        idle('after-' + name)
        assert stable()
        assert row['passed'], name
        print(name, 'PASS', flush=True)
        return out

    def official_summary(path):
        document = read(path)
        rows = [r for r in document['benchmarks'] if r.get('metadata', {}).get('name', document.get('metadata', {}).get('name')) == 'pickle_pure_python']
        assert len(rows) == 1
        metadata = dict(document.get('metadata', {}), **rows[0].get('metadata', {}))
        assert metadata['pickle_module'] == 'pickle' and str(metadata['pickle_protocol']) == '5' and metadata['inner_loops'] == 20
        values = [v for run in rows[0]['runs'] for v in run.get('values', [])]
        assert len(values) == 20 and all(math.isfinite(v) and v > 0 for v in values)
        return dict(output=path.name, sha256=sha(path), values_count=20, mean_seconds=statistics.mean(values),
                    sample_sd_seconds=statistics.stdev(values), metadata=metadata)

    try:
        save()
        idle('preflight')
        for name in ('source_inventory', 'focused_receipt', 'proposal_provenance', 'build_receipt', 'paired_receipt'):
            pin(getattr(args, name), getattr(args, name + '_sha256'))
        pin(PAIRED_CONTROLLER, args.paired_controller_sha256)
        pin(PARENT / 'preserved-release-provenance.json', PARENT_SHA)
        pin(WATCH, WATCH_SHA)
        pin(CP, '4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9')
        pin(CP.with_name('python314.dll'), '0f9857ffdfe010fe6b99328d58c2e3c7472ce75f336bf9c2ad9bd5bca3bce700')
        app, focus, proposal, build, pairs, parent = map(read, (args.source_inventory, args.focused_receipt,
            args.proposal_provenance, args.build_receipt, args.paired_receipt, PARENT / 'preserved-release-provenance.json'))
        assert parent['terminal'] and parent['full_validated'] and (parent['source_count'], parent['file_count'], parent['object_count']) == (117, 178, 149)
        assert set(proposal['candidate_source_sha256']) == TARGETS and proposal['raw_before_sha256'] == parent['source_snapshot_sha256']
        assert set(proposal['new_owned_targets']) == {'tests/fixtures/core/native_str_utf8_encode.py', 'tests/fixtures/expected/native_str_utf8_encode.out'}
        sources = dict(parent['source_snapshot_sha256'], **proposal['candidate_source_sha256'])
        assert len(sources) == 119 and app['source_sha256'] == sources
        assert pairs['terminal'] and pairs['status'] == 'terminal_unscored_paired_original_body_diagnostic' and pairs['hashes_unchanged']
        assert pairs['hashes_before'] == pairs['hashes_after'] and pairs['controller_sha256'] == args.paired_controller_sha256
        assert pairs['parent_manifest_sha256'] == PARENT_SHA and pairs['source_sha256'] == sources
        assert pairs['source_inventory_sha256'] == args.source_inventory_sha256 and pairs['focused_receipt_sha256'] == args.focused_receipt_sha256
        assert pairs['proposal_provenance_sha256'] == args.proposal_provenance_sha256 and pairs['build_receipt_sha256'] == args.build_receipt_sha256
        assert pairs['new_fixture_cases'] == ['native_str_utf8_encode'] and pairs['source_count'] == 119
        assert pairs['trial_decision_sha256'] == '7c0516afb8dea0ecbede40bde8bc8361dd4da2d9728e1ecdb1970c5af8db3fcb'
        assert pairs['encoding_reference_sha256'] == 'f3c91eb53ca3f903838367c2bd111b72ba778ae36d507d88b4f4fa95d0f313e4'
        assert pairs['pair_count'] == 7 and len(pairs['raw']) == 14 and len(pairs['pair_results']) == 7
        assert pairs['values_trimmed'] == pairs['outliers_removed'] == 0 and pairs['original_dump_operations'] == 2460
        assert pairs['decision_rule'] == dict(median_ratio_strictly_greater_than=1.05, lower_ci_strictly_greater_than=1.0,
            bootstrap_resamples=50000, bootstrap_seed=20261008, confidence_percent=95)
        ratios = []
        for index, pair in enumerate(pairs['pair_results']):
            assert pair['pair_index'] == index and pair['order'] == (['parent', 'candidate'] if index % 2 == 0 else ['candidate', 'parent'])
            ratio = pair['runs']['parent']['original_timer_seconds_diagnostic_only'] / pair['runs']['candidate']['original_timer_seconds_diagnostic_only']
            assert ratio == pair['ratio_parent_over_candidate']
            ratios.append(ratio)
        summary = pairs['summary']
        assert ratios == summary['pair_ratios_parent_over_candidate'] and statistics.median(ratios) == summary['median_pair_ratio']
        assert summary['useful_signal'] and summary['median_pair_ratio'] > 1.05 and summary['bootstrap95_ci'][0] > 1.0
        for row in pairs['raw']:
            assert row['passed'] and row['exit_code'] == 0 and not row['timeout'] and row['measurement_valid']
            assert row['owned_child_cleanup_completed'] and row['result_signature'] == pairs['result_signature']
            watch = row['external_process_watch']
            assert watch['measurement_valid'] and not watch['overlaps'] and not watch['scanner_errors']
            pin(DATA / watch['log'], watch['sha256'])
            for stream in ('stdout', 'stderr'):
                pin(DATA / row[stream + '_log'], row[stream + '_sha256'])
        for path, expected in pairs['hashes_after'].items():
            pin(path, expected)
        assert focus['terminal'] and focus['hashes_unchanged'] and focus['status'] == 'targeted_correctness_passed'
        assert focus['source_inventory_sha256'] == args.source_inventory_sha256 and focus['source_sha256'] == sources
        assert focus['binaries_sha256'] == pairs['binaries_sha256']
        assert build['terminal'] and build['status'] == 'build_passed' and build['exit_code'] == build['actual_root_tool_exit_code'] == 0
        assert build['applied_source_sha256'] == args.source_inventory_sha256
        assert build['build_directory'] == 'build-repro/main-verify-20261006' and build['configuration'] == 'Release'
        pin(ROOT / build['log'], build['log_sha256'])
        release = {Path(p).relative_to(RELEASE.relative_to(ROOT)).as_posix(): h for p, h in focus['binaries_sha256'].items()}
        assert len(release) == 178 and len(parent['fixed_baseline_sha256']) == 177
        protected = {str(RELEASE): release, str(BASELINE): parent['fixed_baseline_sha256']}
        protected[str(PARENT)] = dict({'Release/' + p: h for p, h in parent['files_sha256'].items()},
            **{'source-snapshot/' + p: h for p, h in parent['source_snapshot_sha256'].items()},
            **{'native-objects/' + p: h for p, h in parent['object_snapshot_sha256'].items()},
            **{'build-metadata/' + p: h for p, h in parent['build_metadata_sha256'].items()},
            **{'preserved-release-provenance.json': PARENT_SHA})
        for directory, values in protected.items():
            assert tree(Path(directory)) == values
        for path, expected in sources.items():
            pin(ROOT / path, expected)
        spec = importlib.util.spec_from_file_location('utf8_trial_watch', WATCH)
        watcher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(watcher)
        pin(ROOT / 'tests/run_fixtures.py', sources['tests/run_fixtures.py'])
        spec = importlib.util.spec_from_file_location('utf8_full_fixture_inventory', ROOT / 'tests/run_fixtures.py')
        suite = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(suite)
        assert len(suite.CORE_CASES) == 399 and len(suite.SECTION_CASES) == 11
        assert len(set(suite.CORE_CASES)) == 399 and suite.CORE_CASES.count('native_str_utf8_encode') == 1
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
        spec = importlib.util.spec_from_file_location('utf8_default_gate', gate_script)
        gate_module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(gate_module)
        assert len(gate_module.CASES) == 11
        gate_sources = {name: pin(ROOT / 'benchmarks/cases' / (name + '.py')) for name in gate_module.CASES}
        benchmark = CP.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks/bm_pickle'
        pin(benchmark / 'run_benchmark.py', '31c0e30be79514b45db0d4e858632eb429b0fea3edbac6463ad5f34d92f3cab8')
        pin(benchmark / 'bm_pickle_pure_python.toml', '846f31a4f830d4b2ab044917d3b3e6b036ace3647445d770c8980f1f5b158c21')
        pin(CP.parent / 'Lib/pickle.py', '144fdf593814627e13a5fcb0947728e8f0775ecdec181046ee4609063cf232ec')
        pin(ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py', '2ffc217fe20497156bfcb1cebc7fb87203f33c6d4d3dbb6ecbcbcf91ea4cb317')
        pin(RUNNER, RUNNER_SHA)
        pin(ROOT / 'benchmarks/diagnostics/preserve_pyperformance_partial.py')
        pin(CTEST)
        pin(__file__)
        record.update(source_inventory_sha256=args.source_inventory_sha256, source_sha256=sources, source_count=119,
            focused_receipt_sha256=args.focused_receipt_sha256, paired_receipt_sha256=args.paired_receipt_sha256,
            proposal_provenance_sha256=args.proposal_provenance_sha256, build_receipt_sha256=args.build_receipt_sha256,
            parent_manifest_sha256=PARENT_SHA, binaries_sha256=focus['binaries_sha256'],
            baseline_sha256=parent['fixed_baseline_sha256'], fixture_counts=dict(core=399, compatibility_sections=11, expected_failure_cases=3),
            ctest_count=9, native_api_checks=2, hashes_before=dict(pins))
        save()
        authenticate_resume()
        phase('full-fixtures-clean', [CP, ROOT / 'tests/run_fixtures.py', RELEASE / 'xlang3.exe'], 600, expected='')
        regex = '^(' + '|'.join(sorted(CTEST_NAMES)) + ')$'
        ctest = [CTEST, '--test-dir', ROOT / 'build-repro/main-verify-20261006', '-C', 'Release', '-R', regex]
        discovery = phase('ctest-inventory', [*ctest, '--show-only=json-v1'], 120)
        tests = read(discovery)['tests']
        assert {row['name'] for row in tests} == CTEST_NAMES and len(tests) == 9
        ctest_out = phase('ctest', [*ctest, '--output-on-failure'], 300)
        assert '100% tests passed, 0 tests failed out of 9' in ctest_out.read_text(encoding='utf-8')
        assert consumed_resume == set(RESUME_NAMES)
        for name in ('old_xlang_sqlite_api', 'python_sqlite3_api'):
            native = ROOT / 'tests/native/sqlite'
            phase(name, [RELEASE / 'xlang3.exe', native / (name + '.py'), RELEASE / 'modules'], 120,
                  expected=normalized((native / (name + '.out')).read_bytes()))
        record['correctness_passed'] = True
        record['correctness_phase_semantics'] = {row['name']: row.get('semantic_passed', row['passed'])
            for row in record['phases']}
        assert all(record['correctness_phase_semantics'].values())
        save()
        gate_path = DATA / (args.prefix + '-fixed-gate.json')
        try:
            phase('fixed-gate', [CP, gate_script, '--baseline', BASELINE / 'xlang3.exe',
                  '--candidate', RELEASE / 'xlang3.exe', '--output', gate_path], 900)
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
        for name, runtime in (('xlang3', RELEASE / 'xlang3.exe'), ('cpython3147', CP)):
            official = DATA / (args.prefix + '-official-' + name + '-pickle-fast.json')
            try:
                phase('official-' + name, [CP, RUNNER, '--runtime', runtime, '--benchmarks', 'pickle_pure_python',
                    '--mode', 'fast', '--case-timeout', '300', '--dependency-site', SITE, '--output', official], 360)
                record['official_results'][name] = official_summary(official)
                save()
            finally:
                record.setdefault('official_attempts', {})[name] = dict(output=official.name,
                    sha256=sha(official) if official.is_file() else None,
                    complete=name in record['official_results'])
                partial = DATA / (official.stem + '-partial')
                record.setdefault('partial_evidence_sha256', {}).update({p.relative_to(DATA).as_posix(): sha(p)
                    for p in partial.rglob('*') if p.is_file()} if partial.is_dir() else {})
                save()
        cp_time = record['official_results']['cpython3147']['mean_seconds']
        x_time = record['official_results']['xlang3']['mean_seconds']
        record.update(status='trial_validated', full_validated=True,
            comparison=dict(speed_cpython_over_xlang3=cp_time / x_time, time_xlang3_over_cpython=x_time / cp_time,
                scope='Fresh unpaired fast-mode official runs; warnings retained; not a full97 result'))
    except BaseException as error:
        record.update(status='trial_validation_failed', error=type(error).__name__ + ': ' + str(error))
    finally:
        record.update(terminal=True, hashes_after={p: sha(p) if Path(p).is_file() else None for p in pins},
                      hashes_unchanged=stable(), completed_utc=datetime.now(timezone.utc).isoformat())
        if not record['hashes_unchanged']:
            record.update(status='invalid_hash_drift', full_validated=False)
        save()
    return 0 if record['status'] == 'trial_validated' else 1


if __name__ == '__main__':
    raise SystemExit(main())
