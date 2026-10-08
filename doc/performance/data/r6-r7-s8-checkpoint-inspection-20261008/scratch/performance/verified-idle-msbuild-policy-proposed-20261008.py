"""Read-only, narrowly pinned parked MSBuild/conhost activity policy.

Integer process CPU counters are checked before, throughout and after timing.
This does not alter the existing gate/watcher, suspend jobs or ignore a name.
"""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import threading

BUILD_NAMES = frozenset(('cl.exe', 'link.exe', 'ninja.exe', 'cmake.exe', 'ctest.exe',
    'msbuild.exe', 'nmake.exe', 'clang-cl.exe', 'lld-link.exe'))
IDENTITY = ('Name', 'ProcessId', 'ParentProcessId', 'CreationDate', 'ExecutablePath', 'CommandLine')
CPU_FIELDS = ('KernelModeTime', 'UserModeTime')


def integer(value):
    if isinstance(value, bool): raise ValueError('Boolean is not an integer process counter')
    if isinstance(value, int) and value >= 0: return value
    if isinstance(value, str) and re.fullmatch(r'[0-9]+', value): return int(value)
    raise ValueError('Missing or noninteger process identity/counter')


def policy_from_proofs(worker_proof, console_proof):
    worker, parent, console = worker_proof['worker'], worker_proof['parent'], console_proof['console']
    assert worker['Name'].lower() == 'msbuild.exe' and parent['Name'].lower() == 'devenv.exe'
    assert console['Name'].lower() == 'conhost.exe'
    assert re.search(r'(?i)(?:^|\s)/nodemode:1(?:\s|$)', worker['CommandLine'])
    assert re.search(r'(?i)(?:^|\s)/nodeReuse:true(?:\s|$)', worker['CommandLine'])
    assert integer(worker['ParentProcessId']) == integer(parent['ProcessId'])
    assert integer(console['ParentProcessId']) == integer(worker['ProcessId'])
    assert {integer(row['ProcessId']) for row in worker_proof['direct_children']} == {integer(console['ProcessId'])}
    assert console_proof['children'] == []
    assert console_proof['worker_ticks'] == {name: integer(worker[name]) for name in CPU_FIELDS}
    for item in (worker, console):
        for name in IDENTITY: assert item[name] is not None
        for name in CPU_FIELDS: integer(item[name])
    for name in ('Name', 'ProcessId', 'CreationDate', 'ExecutablePath'): assert parent[name] is not None
    return dict(worker=dict(worker), console=dict(console), parent=dict(parent))


def evaluate(snapshot, policy, controller_pid):
    """Pure deterministic policy used by synthetic tests and the real watcher."""
    failures = []
    try:
        if snapshot.get('error'): raise ValueError('scanner-failure')
        rows = snapshot['processes']; by_pid = {}
        for row in rows:
            pid = integer(row['ProcessId']); integer(row['ParentProcessId'])
            if pid in by_pid: raise ValueError('duplicate-process-id')
            assert isinstance(row['Name'], str) and row['Name']
            by_pid[pid] = row
        identities = snapshot['identities']
        def actual_for(expected, role):
            pid = integer(expected['ProcessId'])
            if pid not in by_pid or str(pid) not in identities: raise ValueError(role + '-missing')
            actual = identities[str(pid)]
            for name in (IDENTITY if role != 'parent' else ('Name','ProcessId','CreationDate','ExecutablePath')):
                if actual.get(name) != expected[name]: failures.append(role + '-identity-' + name)
            assert actual['Name'] == by_pid[pid]['Name'] and actual['ProcessId'] == by_pid[pid]['ProcessId']
            assert actual.get('ParentProcessId') == by_pid[pid]['ParentProcessId']
            if role != 'parent':
                for name in CPU_FIELDS:
                    if integer(actual.get(name)) != integer(expected[name]): failures.append(role + '-cpu-' + name)
            return pid
        worker = actual_for(policy['worker'], 'worker')
        console = actual_for(policy['console'], 'console')
        actual_for(policy['parent'], 'parent')
        def descendants(root):
            found, frontier = set(), {root}
            while frontier:
                next_ids = {pid for pid, row in by_pid.items() if integer(row['ParentProcessId']) in frontier}
                if root in next_ids: raise ValueError('cyclic-process-parentage')
                next_ids -= found; found.update(next_ids); frontier = next_ids
            return found
        if descendants(worker) != {console}: failures.append('unknown-worker-descendant')
        if descendants(console): failures.append('console-descendant')
        own = {integer(controller_pid)} | descendants(integer(controller_pid))
        for pid, row in by_pid.items():
            name = row['Name'].lower()
            if name in BUILD_NAMES and pid != worker: failures.append('other-build-' + str(pid))
            if name.startswith(('python', 'xlang3')) and pid not in own: failures.append('other-runtime-' + str(pid))
    except (AssertionError, KeyError, TypeError, ValueError) as error:
        failures.append(str(error) or type(error).__name__)
    return dict(valid=not failures, failures=failures)


def scan(policy):
    # Only the three pinned processes expose executable/cmdline/counters.
    # Unrelated process command lines are neither emitted nor retained.
    ids = [integer(policy[name]['ProcessId']) for name in ('worker', 'console', 'parent')]
    command = r'''$ErrorActionPreference='Stop'; $quietRows=@(Get-CimInstance Win32_Process); $quietIds=IDS;
$quietIdentities=@{}; foreach($quietId in $quietIds){$quietProcess=$quietRows|Where-Object{$_.ProcessId -eq $quietId}; if($quietProcess){$quietIdentities[[string]$quietId]=[ordered]@{Name=$quietProcess.Name;ProcessId=$quietProcess.ProcessId;ParentProcessId=$quietProcess.ParentProcessId;CreationDate=$quietProcess.CreationDate.ToUniversalTime().ToString('o');ExecutablePath=$quietProcess.ExecutablePath;CommandLine=$quietProcess.CommandLine;KernelModeTime=[string]$quietProcess.KernelModeTime;UserModeTime=[string]$quietProcess.UserModeTime}}}; [ordered]@{processes=@($quietRows|Select-Object Name,ProcessId,ParentProcessId);identities=$quietIdentities}|ConvertTo-Json -Depth 5 -Compress'''.replace('IDS', '@(' + ','.join(map(str, ids)) + ')')
    completed = subprocess.run(['powershell', '-NoProfile', '-Command', command], capture_output=True, timeout=10, check=True)
    return json.loads(completed.stdout.decode('utf-8-sig'))


def start_watch(data, prefix, row, policy):
    """Observe only; every counter change, descendant, conflict or error invalidates."""
    path = Path(data) / (prefix + '.verified-worker-observations.jsonl')
    stream = path.open('xb'); stop = threading.Event(); failures = []; count = [0]
    def observe():
        item = dict(observed_utc=datetime.now(timezone.utc).isoformat())
        try:
            item['snapshot'] = scan(policy)
            item['policy'] = evaluate(item['snapshot'], policy, os.getpid())
            if not item['policy']['valid']: failures.append(item['policy'])
        except BaseException as error:
            item['error'] = type(error).__name__ + ': ' + str(error); failures.append(item['error'])
        count[0] += 1; stream.write((json.dumps(item) + '\n').encode('utf-8')); stream.flush()
    def publish():
        row['external_process_watch'] = dict(log=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            measurement_valid=not failures, observations=count[0], failures=list(failures),
            poll_interval_seconds=1, worker_pid=policy['worker']['ProcessId'], console_pid=policy['console']['ProcessId'],
            scope='Only exact pinned worker/console may remain; both integer CPU counters must equal immutable proofs before/during/after; all other build/runtime conflicts, identity changes, unknown descendants and scanner failures invalidate',
            limit='OS snapshots may miss an unrelated process wholly between polls; pinned cumulative CPU counters also checked after child completion')
    observe()
    if failures:
        stream.close(); publish(); raise AssertionError('Pinned worker was not provably idle before timing')
    def loop():
        while not stop.wait(1): observe()
    thread = threading.Thread(target=loop, name='verified-worker-activity-watch', daemon=True); thread.start()
    def finish():
        stop.set(); thread.join(timeout=15)
        if thread.is_alive():
            failures.append('watcher-did-not-stop')
            # Do not race the live stream with another write or close.
            publish(); return False
        observe(); stream.close(); publish(); return not failures
    return finish
