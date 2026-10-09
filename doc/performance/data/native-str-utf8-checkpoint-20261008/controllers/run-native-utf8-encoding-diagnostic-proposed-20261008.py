"""Root-only CPython 3.14.7 then current XLang3 UTF-8 diagnostic, unscored.

Exactly one child per runtime, 5 within-process samples x 20,000 operations per
case after untimed parity. No process-pair inference, score, retry or acceptance.
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
EXE = RELEASE / 'xlang3.exe'
BASELINE = ROOT / 'build-repro/Release'
CHILD = ROOT / 'scratch/performance/native-utf8-encoding-diagnostic-child-proposed-20261008.py'
INPUT_PINS = ROOT / 'scratch/performance/native-utf8-encoding-diagnostic-inputs-proposed-20261008.json'
WATCH = ROOT / 'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'
WATCH_SHA = '50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf'
INVENTORY = DATA / 'dict-scalar-append-runtime-index-r3-applied-source-20261008.json'
INVENTORY_SHA = 'dc1b869fe0475e3bdd017aa3b667f755cb7a4dd7ad881494f8afb34af387f45e'
FOCUS = DATA / 'dict-scalar-append-runtime-index-r3-focused-20261008.json'
FOCUS_SHA = '0737ff2f405151ed29fdfa22bc0177ea77d18f3b63bdf7aff4662972efb94a38'
TRIAL = DATA / 'dict-scalar-append-runtime-index-r3-validation-20261008.json'
TRIAL_SHA = '0b6f05e17c2878efa135656abcce38338934069b59a9460de9f2ba98a8516dd0'
FULL = DATA / 'pyperformance-xlang3-dict-scalar-append-full-fast-20261008-provenance.json'
FULL_SHA = 'afa6a190d0cb545c1ac32e115ed173a17c3776c5223c3e0a6b9cd8352eb58ca8'
sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
read = lambda path: json.loads(Path(path).read_bytes())
tree = lambda path: {p.relative_to(path).as_posix(): sha(p) for p in sorted(path.rglob('*')) if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', default='native-utf8-encoding-current-vs-cpython3147-20261008')
    parser.add_argument('--input-pins-sha256', required=True)
    args = parser.parse_args()
    assert sys.implementation.name == 'cpython' and sys.version_info[:3] == (3, 14, 7)
    assert sys.flags.optimize == 0 and Path(sys.executable).resolve() == CP.resolve()
    assert re.fullmatch(r'[a-z0-9-]+', args.prefix) and not any(DATA.glob(args.prefix + '*'))
    output = DATA / (args.prefix + '.json')
    pins, release, baseline = {}, None, None
    record = {'status': 'preflight', 'terminal': False, 'scored': False, 'acceptance': False,
              'diagnostic_only': True, 'profile_enabled': False, 'raw': [], 'idle_guards': [],
              'operations_per_sample': 20000, 'samples_per_case': 5, 'case_count': 10,
              'timed_operations_per_child': 1000000, 'child_count': 2,
              'scope': 'Local native UTF-8 entry-point costs; no pyperformance or whole-suite claim',
              'started_utc': datetime.now(timezone.utc).isoformat()}

    def save():
        output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))

    def pin(path, expected):
        path = Path(path).resolve(strict=True)
        value = sha(path)
        assert value == expected, str(path)
        assert str(path) not in pins or pins[str(path)] == value
        pins[str(path)] = value

    def stable():
        return (all(Path(p).is_file() and sha(p) == h for p, h in pins.items())
                and tree(RELEASE) == release and tree(BASELINE) == baseline)

    def idle(label):
        raw = subprocess.check_output(['powershell', '-NoProfile', '-Command',
            'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'])
        rows = json.loads(raw.decode('utf-8-sig') or '[]')
        if isinstance(rows, dict):
            rows = [rows]
        tools = {'cl.exe', 'link.exe', 'ninja.exe', 'msbuild.exe', 'cmake.exe', 'ctest.exe',
                 'nmake.exe', 'lld-link.exe', 'clang-cl.exe', 'sample-pprint-native-cpu-20261008.exe'}
        busy = [r for r in rows if r['ProcessId'] != os.getpid() and
                (r['Name'].lower() in tools or r['Name'].lower().startswith(('python', 'xlang3')))]
        record['idle_guards'].append({'phase': label, 'busy': busy, 'controller_pid': os.getpid()})
        save()
        assert not busy, busy

    try:
        save()
        for path, expected in ((INPUT_PINS, args.input_pins_sha256), (INVENTORY, INVENTORY_SHA),
                (FOCUS, FOCUS_SHA), (TRIAL, TRIAL_SHA), (FULL, FULL_SHA), (WATCH, WATCH_SHA)):
            pin(path, expected)
        inputs, inventory, focus, trial, full = map(read, (INPUT_PINS, INVENTORY, FOCUS, TRIAL, FULL))
        for path, expected in inputs['files_sha256'].items():
            pin(path, expected)
        sources = inventory['source_sha256']
        assert len(sources) == inventory['source_count'] == 115
        assert focus['terminal'] and focus['hashes_unchanged'] and focus['status'] == 'targeted_correctness_passed'
        assert focus['source_inventory_sha256'] == INVENTORY_SHA and focus['source_sha256'] == sources
        assert trial['terminal'] and trial['full_validated'] and trial['hashes_unchanged'] and trial['status'] == 'trial_validated'
        assert trial['source_inventory_sha256'] == INVENTORY_SHA and trial['source_sha256'] == sources
        assert full['terminal'] and full['hashes_unchanged'] and full['source_sha256'] == sources
        assert full['source_inventory_sha256'] == INVENTORY_SHA and full['timing_measurement_valid']
        release, baseline = tree(RELEASE), tree(BASELINE)
        assert len(release) == 178 and len(baseline) == 177
        assert release == {Path(p).relative_to(RELEASE).as_posix(): h
                           for p, h in ((ROOT / p, h) for p, h in focus['binaries_sha256'].items())}
        assert full['binaries_sha256'] == focus['binaries_sha256'] and baseline == full['baseline_sha256']
        for path, expected in sources.items():
            pin(ROOT / path, expected)
        for path, expected in release.items():
            pin(RELEASE / path, expected)
        for path, expected in baseline.items():
            pin(BASELINE / path, expected)
        assert sha(EXE) == focus['candidate_binary_sha256']['exe']
        assert sha(EXE.with_name('xlang3_runtime.dll')) == focus['candidate_binary_sha256']['dll']
        record.update(source_inventory_sha256=INVENTORY_SHA, source_sha256=sources,
                      source_count=115, additional_source_sha256=inputs['additional_source_sha256'],
                      binaries_sha256=focus['binaries_sha256'], baseline_sha256=baseline,
                      candidate_binary_sha256=focus['candidate_binary_sha256'], child_sha256=sha(CHILD),
                      input_pins_sha256=args.input_pins_sha256, controller_sha256=sha(__file__),
                      provenance_limit='Source115 inventory plus two explicitly additional encoding sources; not complete compiled-source coverage')
        pin(__file__, sha(__file__))
        spec = importlib.util.spec_from_file_location('native_utf8_existing_watch', WATCH)
        watcher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(watcher)
        environment = dict(os.environ)
        for name in ('PYTHONOPTIMIZE', 'PYTHONPATH', 'PYTHONHOME', 'PYTHONPYCACHEPREFIX',
                     'PYTHONIOENCODING', '_NT_SYMBOL_PATH', '_NT_ALT_SYMBOL_PATH'):
            environment.pop(name, None)
        assert not environment.get('XLANG3_VM_OPCODE_TIMING') and not environment.get('XLANG3_PERF')
        environment.update(XLANG3_PYTHON_LIB=str(CP.parent / 'Lib'), PYTHONUNBUFFERED='1', PYTHONIOENCODING='utf-8')
        record['hashes_before'] = dict(pins)
        idle('preflight')
        assert stable()
        for role, executable in (('cpython', CP), ('xlang3', EXE)):
            label = role
            idle('before-' + label)
            assert stable()
            cache = ROOT / 'scratch/performance' / ('pycache-' + args.prefix + '-' + role)
            assert not cache.exists(), 'Fresh per-child pycache required'
            env = dict(environment, PYTHONPYCACHEPREFIX=str(cache))
            command = ([str(CP), '-I', '-u', '-X', 'pycache_prefix=' + str(cache), str(CHILD)]
                       if role == 'cpython' else [str(EXE), str(CHILD)])
            stdout = DATA / (args.prefix + '-' + label + '.stdout.log')
            stderr = DATA / (args.prefix + '-' + label + '.stderr.log')
            row = {'runtime': role, 'command': command, 'stdout_log': stdout.name, 'stderr_log': stderr.name,
                   'timeout': False, 'passed': False, 'pycache_prefix': str(cache), 'timeout_seconds': 120}
            record['raw'].append(row)
            save()
            child = None
            finish_watch = watcher.start_timing_process_watch(args.prefix, label, row)
            try:
                with stdout.open('xb') as out, stderr.open('xb') as err:
                    child = subprocess.Popen(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                                             stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
                    row['pid'] = child.pid
                    save()
                    try:
                        row['exit_code'] = child.wait(timeout=120)
                    except subprocess.TimeoutExpired:
                        row['timeout'] = True
                assert row.get('exit_code') == 0 and not row['timeout']
                events = [json.loads(line) for line in stdout.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
                row['events'] = events
                assert stderr.read_bytes() == b'' and len(events) == 2
                assert events[0]['status'] == 'diagnostic_start' and events[1]['status'] == 'diagnostic_complete'
                for event in events:
                    assert event['runtime'] == role and Path(event['executable']).resolve() == executable.resolve()
                    assert event['version_info'] == [3, 14, 7] and event['optimization_level'] == 0
                    assert not event['profile_enabled'] and not event['trace_enabled']
                    assert event['child_sha256'] == sha(CHILD)
                    assert (event['operations_per_sample'], event['samples_per_case'], event['case_count'], event['timed_operations']) == (20000, 5, 10, 1000000)
                    assert event['input_signature'] == inputs['input_signature']
                event = events[1]
                assert event['success'] and event['child_hash_unchanged']
                assert event['parity'] == inputs['expected_parity'] and event['strict_errors'] == inputs['expected_strict_errors']
                assert event['provider']['module'] == '_codecs' and event['provider']['function_name'] == 'utf_8_encode'
                assert not event['provider']['python_code'] and len(event['samples']) == 50
                cases = inputs['case_order']
                expected_order = [(index, name, route) for index in range(5)
                                  for name, route in (cases if index % 2 == 0 else list(reversed(cases)))]
                assert [(r['sample_index'], r['input'], r['route']) for r in event['samples']] == expected_order
                for sample in event['samples']:
                    assert math.isfinite(sample['elapsed_seconds']) and sample['elapsed_seconds'] > 0
                    assert sample['output_signature'] == inputs['output_signature'][sample['input']]
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
                    for stream, path in (('stdout', stdout), ('stderr', stderr)):
                        try:
                            row[stream + '_sha256'] = sha(path) if path.is_file() else None
                        except BaseException as error:
                            row[stream + '_sha256'] = None
                            row[stream + '_hash_error'] = type(error).__name__ + ': ' + str(error)
                            row['passed'] = False
                    try:
                        row['measurement_valid'] = finish_watch()
                    except BaseException as error:
                        row.update(measurement_valid=False, watch_finish_error=type(error).__name__ + ': ' + str(error))
                    row['passed'] = row['passed'] and row['measurement_valid']
                    save()
            idle('after-' + label)
            assert stable()
            assert row['passed'], 'Failed/invalid child retained; no retry or replacement sample'
        assert record['raw'][0]['events'][1]['parity'] == record['raw'][1]['events'][1]['parity']
        medians = {}
        for row in record['raw']:
            medians[row['runtime']] = {name + ':' + route: statistics.median([
                r['elapsed_seconds'] for r in row['events'][1]['samples'] if r['input'] == name and r['route'] == route])
                for name, route in inputs['case_order']}
        record['summary'] = {'median_seconds_per_20000_operations': medians,
            'xlang3_over_cpython_elapsed_ratio_diagnostic_only': {name: medians['xlang3'][name] / medians['cpython'][name] for name in medians['cpython']},
            'method_over_direct_codec_ratio_within_runtime': {role: {name: values[name + ':method_surrogatepass'] / values[name + ':codecs_surrogatepass'] for name in ('ascii', 'multibyte', 'surrogate')} for role, values in medians.items()},
            'limits': 'Five within-process samples, one process per runtime in CP-first order; no CI, acceptance, whole-case gain or CP-win claim'}
        record['status'] = 'terminal_unscored_native_utf8_parity_and_timing'
    except BaseException as error:
        record.update(status='terminal_failed_diagnostic', error=type(error).__name__ + ': ' + str(error))
    finally:
        record['hashes_after'] = {p: sha(p) if Path(p).is_file() else None for p in pins}
        record['input_verification_complete'] = release is not None and baseline is not None
        record['hashes_unchanged'] = stable() if record['input_verification_complete'] else None
        if record['hashes_unchanged'] is False:
            record['status'] = 'terminal_invalid_hash_drift'
        record.update(terminal=True, completed_utc=datetime.now(timezone.utc).isoformat())
        save()
    return 0 if record['status'] == 'terminal_unscored_native_utf8_parity_and_timing' else 1


if __name__ == '__main__':
    raise SystemExit(main())
