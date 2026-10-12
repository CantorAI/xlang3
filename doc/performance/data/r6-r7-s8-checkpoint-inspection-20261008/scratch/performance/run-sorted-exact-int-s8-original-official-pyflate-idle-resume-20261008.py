"""One fresh original official pyflate attempt with exact parked-worker activity checks.

Exact CPython 3.14.7 is the manager only. No correctness, CP benchmark, build,
retry, threshold adjustment or modified decoder is launched here.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
EXE = RELEASE / 'xlang3.exe'
SITE = ROOT / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'
BENCH = CP.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks/bm_pyflate'
CP_FULL = DATA / 'pyperformance-cpython3147-live-eval-full-fast-20261007.json'
CP_PROOF = CP_FULL.with_name(CP_FULL.stem + '-provenance.json')
HOOK = ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
SHIM = ROOT / 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py'
WATCH = ROOT / 'scratch/performance/verified-idle-msbuild-policy-proposed-20261008.py'
WATCH_SHA = '4063d997ff08551eba3a909e3b897758518854645a6521efee32bda0307f5be4'
TEST_SOURCE = ROOT / 'scratch/performance/test-verified-idle-msbuild-policy-proposed-20261008.py'
TEST_SOURCE_SHA = '7d6891b513e689c9346f14fa5c6f4f0b533fdc750412a151822f227db0e9295e'
WORKER_PROOF = DATA / 'verified-idle-msbuild-worker-20261008.json'
WORKER_PROOF_SHA = '5f9522080e4309aba05149fdbddf2daeaaef0c8fbdefc18d7cf713d4750ec210'
CONSOLE_PROOF = DATA / 'verified-idle-msbuild-console-20261008.json'
CONSOLE_PROOF_SHA = '63a7bc6f8f91b82c7a5c8136aff8f403179ea7bd6e67c8956a148e265c5ddeb1'
FAILED = DATA / 'sorted-exact-int-s8-original-official-pyflate-20261008.json'
FAILED_SHA = '545c506ae452bacf2c993fb35253c0d59c430f146e4147ab2517220c48515881'
FAILED_CONTROLLER = ROOT / 'scratch/performance/run-sorted-exact-int-s8-original-official-pyflate-20261008.py'
FULL_CONTROLLER = ROOT / 'scratch/performance/validate-sorted-exact-int-s8-full-20261008.py'
SOURCE_SHA = 'ec5347c7045af86ba33e1b2c6f64bc4b506d0ade3b77391d7e41863fe25fb464'
INPUT_SHA = '81101162ee7fc7a3db86d1a87e0c86781304eb9026aca4078432004a1383c51a'
DEFINITION_SHA = 'ea7eb22536c8fd9aa822abf9d60192b4787de2a2258f059a363e99d8598bdcac'
PREFIX = 'sorted-exact-int-s8-original-official-pyflate-idle-resume-20261008'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def pyflate_values(document):
    rows = [row for row in document['benchmarks']
            if row.get('metadata', {}).get('name', document.get('metadata', {}).get('name')) == 'pyflate']
    assert len(rows) == 1
    values = [value for run in rows[0]['runs'] for value in run.get('values', [])]
    assert len(values) == 20 and all(math.isfinite(value) and value > 0 for value in values)
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validation', type=Path, required=True)
    parser.add_argument('--validation-sha256', required=True)
    parser.add_argument('--source-inventory', type=Path, required=True)
    parser.add_argument('--source-inventory-sha256', required=True)
    parser.add_argument('--policy-test-receipt', type=Path, required=True)
    parser.add_argument('--policy-test-receipt-sha256', required=True)
    args = parser.parse_args()
    assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CP.resolve()
    assert sys.flags.optimize == 0 and not any(DATA.glob(PREFIX + '*')), 'Never overwrite or repeat this attempt'
    output = DATA / (PREFIX + '.json')
    official = DATA / (PREFIX + '-fast.json')
    record = dict(status='preflight', terminal=False, full_correctness_reexecuted=False,
        case='pyflate', mode='fast', case_timeout_seconds=300, outer_timeout_seconds=360,
        raw=[], idle_guards=[], started_utc=datetime.now(timezone.utc).isoformat(),
        scope='One original official pure-Python decoder case after current S8 correctness/default gate; saved Oct7 CP row is unpaired; no overall CPython-win or full97 claim')
    pins = {}
    watcher = None
    activity_policy = None

    def pin(path, expected=None):
        path = Path(path).resolve(strict=True); actual = sha(path)
        if expected is not None: assert actual == expected, str(path)
        if str(path) in pins: assert pins[str(path)] == actual
        pins[str(path)] = actual
        return actual

    release_map = lambda: {p.relative_to(ROOT).as_posix(): sha(p) for p in RELEASE.rglob('*') if p.is_file()}
    def stable():
        return all(Path(p).is_file() and sha(p) == value for p, value in pins.items()) and release_map() == record.get('binaries_sha256')
    def save():
        output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))
    def idle(label):
        snapshot = watcher.scan(activity_policy)
        result = watcher.evaluate(snapshot, activity_policy, os.getpid())
        record['idle_guards'].append(dict(phase=label, snapshot=snapshot, policy=result)); save()
        assert result['valid'], result

    try:
        validation_path = args.validation.resolve(strict=True)
        inventory_path = args.source_inventory.resolve(strict=True)
        pin(validation_path, args.validation_sha256); pin(inventory_path, args.source_inventory_sha256)
        pin(Path(__file__)); pin(FULL_CONTROLLER, '997d0e694f5c04aa2459bfbaca36065054c53d1661f810e37c5f7dd7b50813f3')
        pin(FAILED, FAILED_SHA); pin(FAILED_CONTROLLER, '3dbfc68c563c30b615d7a4770b891f984b9f576fa51b49985ae78ba8b060ef29')
        failed = json.loads(FAILED.read_bytes())
        assert failed['terminal'] and failed['hashes_unchanged'] and failed['raw'] == []
        assert failed['status'] == 'terminal_failed_official_pyflate_preflight_or_execution'
        assert failed['idle_guards'][-1]['busy'] == [dict(Name='MSBuild.exe', ProcessId=30756)]
        pin(WATCH, WATCH_SHA); pin(TEST_SOURCE, TEST_SOURCE_SHA)
        tests_path = args.policy_test_receipt.resolve(strict=True); pin(tests_path, args.policy_test_receipt_sha256)
        tests = json.loads(tests_path.read_bytes())
        assert tests['terminal'] and tests['status'] == 'terminal_policy_tests_passed'
        assert tests['cpython_version'].startswith('3.14.7 ') and tests['helper_sha256'] == WATCH_SHA
        assert tests['test_source_sha256'] == TEST_SOURCE_SHA and tests['test_count'] == 26
        assert len(tests['cases']) == 26 and all(case['passed'] for case in tests['cases'])
        pin(WORKER_PROOF, WORKER_PROOF_SHA); pin(CONSOLE_PROOF, CONSOLE_PROOF_SHA)
        spec = importlib.util.spec_from_file_location('verified_worker_activity', WATCH)
        watcher = importlib.util.module_from_spec(spec); spec.loader.exec_module(watcher)
        activity_policy = watcher.policy_from_proofs(json.loads(WORKER_PROOF.read_bytes()), json.loads(CONSOLE_PROOF.read_bytes()))
        record['prior_failed_preflight'] = dict(record=FAILED.name, sha256=FAILED_SHA, timed_child_started=False)
        record['required_activity_policy'] = dict(source_sha256=WATCH_SHA, test_source_sha256=TEST_SOURCE_SHA,
            test_receipt=str(tests_path), test_receipt_sha256=sha(tests_path), worker_proof_sha256=WORKER_PROOF_SHA,
            console_proof_sha256=CONSOLE_PROOF_SHA, pinned_worker_pid=30756, pinned_console_pid=9220,
            scope='Only exact immutable worker+console identities and unchanged integer kernel/user CPU counters; any activity, unknown descendants, other builds/runtime conflicts or scanner failure invalidates')
        full = json.loads(validation_path.read_bytes()); inv = json.loads(inventory_path.read_bytes())
        assert full['terminal'] and full['correctness_passed'] and full['hashes_unchanged'] and full['release_tree_unchanged']
        assert failed['source_inventory_sha256'] == sha(inventory_path) and failed['source_sha256'] == inv['source_sha256']
        assert full['status'] in ('validated', 'correctness_and_gate_passed_official_failed')
        assert full['terminal_record']['controller_sha256'] == sha(FULL_CONTROLLER)
        assert full['hashes_before'] == full['hashes_after']
        assert inv['source_count'] == len(inv['source_sha256']) == 110
        assert full['source_sha256'] == inv['source_sha256'] and full['source_inventory_sha256'] == sha(inventory_path)
        assert full['fixture_counts'] == dict(core=398, compatibility_sections=11, expected_failures=3)
        assert len(full['registered_ctest_names']) == 9 and full['fixed_gate']['exit_code'] == 0
        for path, value in full['hashes_before'].items(): pin(Path(path), value)
        for path, value in inv['source_sha256'].items(): pin(ROOT / path, value)
        binaries = release_map(); assert len(binaries) == 178 and binaries == full['binaries_sha256']
        record['binaries_sha256'] = binaries
        for path, value in binaries.items(): pin(ROOT / path, value)
        assert full['candidate_binary_sha256'] == dict(exe=sha(EXE), dll=sha(RELEASE / 'xlang3_runtime.dll'))
        gate_path = DATA / full['fixed_gate']['output']; pin(gate_path, full['fixed_gate']['sha256'])
        gate = json.loads(gate_path.read_bytes())
        assert gate['status'] == 'pass' and len(gate['cases']) == 11 and (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .1)
        assert set(gate['cases']) == set(full['fixed_gate_source_sha256'])
        assert all(row['source_sha256'] == full['fixed_gate_source_sha256'][name] for name, row in gate['cases'].items())
        for row in full['phases']:
            for stream in ('stdout', 'stderr'): pin(DATA / row[stream + '_log'], row[stream + '_sha256'])
            if 'external_process_watch' in row:
                watch = row['external_process_watch']; pin(DATA / watch['log'], watch['sha256'])
                assert watch['measurement_valid'], 'Prior checkpoint timing was invalid'
        record['required_current_checkpoint'] = dict(record=str(validation_path), sha256=sha(validation_path),
            status=full['status'], source_inventory_sha256=sha(inventory_path), fixed_gate=dict(full['fixed_gate']),
            scope='Matching prior correctness/default gate proof only; no old failure upgraded and no correctness phase rerun')
        record['source_inventory_sha256'] = sha(inventory_path); record['source_sha256'] = inv['source_sha256']
        pin(CP_PROOF, '3d6c8cb2c72c786583c382b835e8e411f56ca285963bc2835f757a13f399f85c')
        pin(CP_FULL, 'ac474df5576ac85f6f5350363affecc439d4f658b4dfa3dbe291b46964276eb0')
        cp_proof = json.loads(CP_PROOF.read_bytes())
        assert cp_proof['status'] == 'finished' and cp_proof['runtime_version'] == '3.14.7'
        pin(CP, cp_proof['sha256_start']['exe']); pin(CP.with_name('python314.dll'), cp_proof['sha256_start']['dll'])
        pin(HOOK, cp_proof['compatibility_hook_sha256']); pin(SHIM)
        pin(ROOT / 'benchmarks/diagnostics/preserve_pyperformance_partial.py')
        assert cp_proof['benchmark_python_sources']['bm_pyflate\\run_benchmark.py'] == SOURCE_SHA
        pin(BENCH / 'run_benchmark.py', SOURCE_SHA); pin(BENCH / 'pyproject.toml', DEFINITION_SHA)
        pin(BENCH / 'data/interpreter.tar.bz2', INPUT_SHA)
        import tomllib
        definition = tomllib.loads((BENCH / 'pyproject.toml').read_text(encoding='utf-8'))['tool']['pyperformance']
        assert definition['name'] == 'pyflate' and not definition.get('extra_opts')
        reference_values = pyflate_values(json.loads(CP_FULL.read_bytes()))
        for path, value in cp_proof['dependency_metadata_sha256'].items(): pin(SITE / path, value)
        record['preserved_cp_reference'] = dict(output=CP_FULL.name, sha256=sha(CP_FULL), values=reference_values,
            mean_seconds=sum(reference_values)/len(reference_values), scope='Saved Oct7 exact CPython3.14.7 official row; unpaired; dependency METADATA does not prove historical package source/data bytes')
        record['benchmark_identity'] = dict(source=str(BENCH/'run_benchmark.py'), source_sha256=SOURCE_SHA,
            definition_sha256=DEFINITION_SHA, input=str(BENCH/'data/interpreter.tar.bz2'), input_sha256=INPUT_SHA,
            checksum='afa004a630fe072901b1d9628b960974', pure_python_decoder_unchanged=True)
        record['hashes_before'] = dict(pins); save(); idle('before-original-pyflate'); assert stable()
        env = os.environ.copy()
        for name in ('PYTHONOPTIMIZE','PYTHONPYCACHEPREFIX','PYTHONPATH'): env.pop(name, None)
        assert not env.get('XLANG3_VM_OPCODE_TIMING'), 'Original official case must be uninstrumented'
        env.update(XLANG3_PYTHON_LIB='C:/Python/Python314/Lib', PYTHONPATH=str(HOOK.parent), PYTHONIOENCODING='utf-8')
        stdout = DATA/(PREFIX+'.stdout.log'); stderr = DATA/(PREFIX+'.stderr.log')
        row = dict(name='official-pyflate', command=[str(CP), str(SHIM), '--runtime', str(EXE),
            '--benchmarks', 'pyflate', '--mode', 'fast', '--case-timeout', '300', '--dependency-site', str(SITE), '--output', str(official)],
            timeout=False, timeout_seconds=360, passed=False, stdout_log=stdout.name, stderr_log=stderr.name)
        record['raw'].append(row); record['status'] = 'running_original_official_pyflate'; save()
        finish = watcher.start_watch(DATA, PREFIX, row, activity_policy); child = None
        try:
            with stdout.open('xb') as out, stderr.open('xb') as err:
                child = subprocess.Popen(row['command'], cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                    stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
                row['pid'] = child.pid; save()
                try: row['exit_code'] = child.wait(timeout=360)
                except subprocess.TimeoutExpired:
                    row['timeout'] = True
                    subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)], capture_output=True, timeout=15)
                    if child.poll() is None: child.kill()
                    row['exit_code'] = child.wait(timeout=15)
        finally:
            if child is not None and child.poll() is None:
                subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)], capture_output=True, timeout=15)
                if child.poll() is None: child.kill()
                child.wait(timeout=15)
            row['measurement_valid'] = finish()
            row['stdout_sha256'] = sha(stdout); row['stderr_sha256'] = sha(stderr)
        row['passed'] = row['exit_code'] == 0 and not row['timeout'] and row['measurement_valid']
        score = dict(output=official.name, sha256=sha(official) if official.is_file() else None, complete=False)
        if row['passed'] and official.is_file():
            try:
                values = pyflate_values(json.loads(official.read_bytes()))
                score.update(complete=True, values=values, values_count=20, mean_seconds=sum(values)/len(values),
                    saved_cp_over_x=(sum(reference_values)/len(reference_values))/(sum(values)/len(values)))
            except Exception as error: score['validation_error'] = repr(error)
        record['official_pyflate'] = score
        record['status'] = 'terminal_complete_original_official_pyflate' if score['complete'] else 'terminal_original_official_pyflate_failed_or_incomplete'
        save(); idle('after-original-pyflate')
    except BaseException as error:
        record.update(status='terminal_failed_official_pyflate_preflight_or_execution', error=repr(error))
        if 'official_pyflate' in record:
            record['official_pyflate'].update(complete=False, invalidated_by_guard_or_execution=True)
    finally:
        partial = official.parent/(official.stem+'-partial')
        record['partial_evidence_sha256'] = {p.relative_to(DATA).as_posix():sha(p) for p in partial.rglob('*') if p.is_file()} if partial.is_dir() else {}
        record['hashes_after'] = {p:sha(p) if Path(p).is_file() else None for p in pins}
        record['hashes_unchanged'] = record['hashes_after'] == pins and release_map() == record.get('binaries_sha256')
        if not record['hashes_unchanged']: record['status'] = 'terminal_invalid_hash_drift'
        record.update(terminal=True, completed_utc=datetime.now(timezone.utc).isoformat()); save()
    print(record['status'], record.get('official_pyflate', {}), flush=True)
    return 0 if record['status'] == 'terminal_complete_original_official_pyflate' else 1


if __name__ == '__main__':
    raise SystemExit(main())
