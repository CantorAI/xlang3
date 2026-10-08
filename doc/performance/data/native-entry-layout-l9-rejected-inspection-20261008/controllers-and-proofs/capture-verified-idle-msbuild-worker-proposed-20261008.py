"""Two read-only CPU/identity snapshots; root launches only, never a benchmark."""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
HELPER = ROOT / 'scratch/performance/verified-idle-msbuild-policy-proposed-20261008.py'
HELPER_SHA = '4063d997ff08551eba3a909e3b897758518854645a6521efee32bda0307f5be4'
TESTS = DATA / 'verified-idle-msbuild-policy-tests-20261008.json'
TESTS_SHA = 'c78a22720be14a6f447d19ae4a5ecde8a63efda5d555bc037b5782a416ddd88f'
SHA = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()


def first_snapshot(worker_pid):
    # Emit full fields only for the requested worker, its direct child and parent.
    # Unrelated command lines are never exposed or retained.
    command = r'''$ErrorActionPreference='Stop'; $captureRows=@(Get-CimInstance Win32_Process);
$captureWorker=@($captureRows|Where-Object{$_.ProcessId -eq WORKER});
if($captureWorker.Count -ne 1){throw 'Requested worker missing or duplicated'};
$captureChildren=@($captureRows|Where-Object{$_.ParentProcessId -eq WORKER});
$captureIds=@(WORKER,$captureWorker[0].ParentProcessId)+@($captureChildren.ProcessId);
$captureIdentities=@{}; foreach($captureId in $captureIds){$captureProcess=$captureRows|Where-Object{$_.ProcessId -eq $captureId};
if($captureProcess){$captureIdentities[[string]$captureId]=[ordered]@{Name=$captureProcess.Name;ProcessId=$captureProcess.ProcessId;ParentProcessId=$captureProcess.ParentProcessId;CreationDate=$captureProcess.CreationDate.ToUniversalTime().ToString('o');ExecutablePath=$captureProcess.ExecutablePath;CommandLine=$captureProcess.CommandLine;KernelModeTime=[string]$captureProcess.KernelModeTime;UserModeTime=[string]$captureProcess.UserModeTime}}};
[ordered]@{processes=@($captureRows|Select-Object Name,ProcessId,ParentProcessId);identities=$captureIdentities}|ConvertTo-Json -Depth 5 -Compress'''.replace('WORKER', str(worker_pid))
    completed = subprocess.run(['powershell', '-NoProfile', '-Command', command],
        capture_output=True, check=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
    return json.loads(completed.stdout.decode('utf-8-sig'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker-pid', type=int, required=True)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    assert sys.version_info[:3] == (3, 14, 7) and not sys.flags.optimize
    assert Path(sys.executable).resolve() == CP.resolve()
    assert args.worker_pid > 0 and re.fullmatch(r'[a-z0-9-]+', args.prefix)
    assert not any(DATA.glob(args.prefix + '*')), 'Preserve earlier capture; no overwrite or automatic retry'
    assert SHA(HELPER) == HELPER_SHA and SHA(TESTS) == TESTS_SHA
    tests = json.loads(TESTS.read_bytes())
    assert tests['terminal'] and tests['status'] == 'terminal_policy_tests_passed' and tests['test_count'] == 26
    assert tests['helper_sha256'] == HELPER_SHA and all(case['passed'] for case in tests['cases'])
    spec = importlib.util.spec_from_file_location('verified_activity_policy', HELPER)
    helper = importlib.util.module_from_spec(spec); spec.loader.exec_module(helper)
    output = DATA / (args.prefix + '-capture.json')
    worker_path = DATA / (args.prefix + '-worker.json')
    console_path = DATA / (args.prefix + '-console.json')
    record = dict(status='capturing', terminal=False, snapshots=[], interval_seconds=10,
        worker_pid=args.worker_pid, helper_sha256=HELPER_SHA, tests_sha256=TESTS_SHA,
        controller_sha256=SHA(__file__), cpython_version=sys.version, benchmark_executed=False,
        scope='Two identity/CPU observations only; quiet capture is not future timing approval',
        started_utc=datetime.now(timezone.utc).isoformat())
    def save(): output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))
    try:
        first = first_snapshot(args.worker_pid)
        record['snapshots'].append(dict(observed_utc=datetime.now(timezone.utc).isoformat(), snapshot=first)); save()
        worker = dict(first['identities'][str(args.worker_pid)])
        children = [row for row in first['processes'] if row['ParentProcessId'] == args.worker_pid]
        assert len(children) == 1 and children[0]['Name'].lower() == 'conhost.exe', 'Require exactly one conhost child'
        console = dict(first['identities'][str(children[0]['ProcessId'])])
        parent = dict(first['identities'][str(worker['ParentProcessId'])])
        for item in (worker, console):
            for name in helper.CPU_FIELDS: item[name] = helper.integer(item[name])
        console_children = [row for row in first['processes'] if row['ParentProcessId'] == console['ProcessId']]
        worker_proof = dict(status='observed_reusable_worker_identity_only_activity_must_be_revalidated',
            worker=worker, parent=parent, direct_children=children)
        console_proof = dict(status='console_identity_observation_only_activity_must_be_revalidated',
            console=console, worker_ticks={name:worker[name] for name in helper.CPU_FIELDS}, children=console_children)
        policy = helper.policy_from_proofs(worker_proof, console_proof)
        before = helper.evaluate(first, policy, os.getpid()); record['snapshots'][0]['policy'] = before; save()
        assert before['valid'], before
        start = time.monotonic(); time.sleep(10)
        second = helper.scan(policy)
        after = helper.evaluate(second, policy, os.getpid())
        record['snapshots'].append(dict(observed_utc=datetime.now(timezone.utc).isoformat(), snapshot=second, policy=after))
        record['actual_interval_seconds'] = time.monotonic() - start
        assert record['actual_interval_seconds'] >= 10 and after['valid'], after
        # Capture all six parent identity fields as requested. The existing tested
        # timing policy intentionally checks the parent's four original fields.
        current_parent = second['identities'][str(parent['ProcessId'])]
        assert all(current_parent.get(name) == parent.get(name) for name in helper.IDENTITY), 'Parent identity changed'
        assert SHA(HELPER) == HELPER_SHA and SHA(TESTS) == TESTS_SHA and SHA(__file__) == record['controller_sha256']
        record.update(status='terminal_two_snapshot_quiet_worker_capture_passed', terminal=True,
            console_pid=console['ProcessId'], parent_pid=parent['ProcessId'],
            completed_utc=datetime.now(timezone.utc).isoformat())
        save()
        capture_sha = SHA(output)
        for path, proof in ((worker_path, worker_proof), (console_path, console_proof)):
            proof.update(capture_record=output.name, capture_sha256=capture_sha,
                interval_seconds=record['actual_interval_seconds'], two_snapshots_passed=True,
                helper_sha256=HELPER_SHA, tests_sha256=TESTS_SHA)
            with path.open('xb') as stream: stream.write((json.dumps(proof, indent=2) + '\n').encode('utf-8'))
        print(record['status'], 'worker', args.worker_pid, 'console', console['ProcessId'], flush=True)
        return 0
    except BaseException as error:
        record.update(status='terminal_failed_quiet_worker_capture', terminal=True,
            error=type(error).__name__ + ': ' + str(error), completed_utc=datetime.now(timezone.utc).isoformat())
        save(); print(record['status'], flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
