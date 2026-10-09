"""Detect actual build overlap, with one continuously verified dormant node exception."""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import threading
import time

DATA = Path('D:/CantorAI/xlang3/doc/performance/data')
TOOLS = {'cl.exe', 'link.exe', 'ninja.exe', 'msbuild.exe', 'cmake.exe', 'ctest.exe',
         'nmake.exe', 'lld-link.exe', 'clang-cl.exe'}
SCAN = ("Get-CimInstance Win32_Process | Select-Object Name,ProcessId,ParentProcessId,"
        "CreationDate,KernelModeTime,UserModeTime,@{Name='CommandLine';Expression={"
        "if($_.Name -ieq 'MSBuild.exe'){$_.CommandLine}else{''}}} | ConvertTo-Json -Compress")
FIELDS = ('Name', 'ProcessId', 'ParentProcessId', 'CreationDate', 'KernelModeTime', 'UserModeTime', 'CommandLine')
_admission = None

def scan():
    raw = subprocess.check_output(['powershell', '-NoProfile', '-Command', SCAN], timeout=10)
    rows = json.loads(raw.decode('utf-8-sig') or '[]')
    return [rows] if isinstance(rows, dict) else rows

def _same_dormant(row, rows):
    if _admission is None: return False
    expected = _admission['worker']
    if any(row.get(k) != expected.get(k) for k in FIELDS): return False
    if any(p['ProcessId'] == row['ParentProcessId'] for p in rows): return False
    expected_children = {p['ProcessId']: p for p in _admission['children']}
    for child in rows:
        if child['ParentProcessId'] != row['ProcessId']: continue
        prior = expected_children.get(child['ProcessId'])
        if prior is None or child['Name'].lower() != 'conhost.exe': return False
        if any(child.get(k) != prior.get(k) for k in FIELDS): return False
    return True

def admit_dormant_worker(worker_pid):
    global _admission
    first = scan()
    worker = next((p for p in first if p['ProcessId'] == worker_pid), None)
    if worker is None:
        return {'status': 'known_worker_already_exited', 'exception_used': False}
    assert worker['Name'].lower() == 'msbuild.exe'
    assert '/nodemode:1' in worker['CommandLine'] and '/nodeReuse:true' in worker['CommandLine']
    assert not any(p['ProcessId'] == worker['ParentProcessId'] for p in first)
    children = [p for p in first if p['ParentProcessId'] == worker['ProcessId']]
    assert all(p['Name'].lower() == 'conhost.exe' for p in children)
    _admission = {'status': 'orphaned_cached_worker_verified_idle', 'exception_used': True,
        'worker': worker, 'children': children, 'verified_utc': datetime.now(timezone.utc).isoformat(),
        'scope': 'Only this exact node identity. Any lifetime CPU-counter change, new child, changed command or live parent invalidates timing; no counter reset between phases.'}
    time.sleep(2)
    second = scan()
    row = next((p for p in second if p['ProcessId'] == worker['ProcessId']), None)
    assert row is None or _same_dormant(row, second), 'Worker activity during admission'
    return _admission

def classify(rows, include_runtimes=False):
    busy, dormant = [], []
    for row in rows:
        name = row['Name'].lower()
        if name in TOOLS:
            if name == 'msbuild.exe' and _same_dormant(row, rows): dormant.append(row)
            else: busy.append(row)
        elif include_runtimes and row['ProcessId'] != os.getpid() and name.startswith(('python', 'xlang3')):
            # Keep commands out of exported observations for unrelated apps.
            busy.append({k: row[k] for k in ('Name', 'ProcessId', 'ParentProcessId')})
    return {'busy': busy, 'dormant': dormant}

def idle_snapshot():
    return classify(scan(), include_runtimes=True)

def start_timing_process_watch(prefix, name, phase_row):
    path = DATA / (prefix + '-' + name + '.external-process-observations.jsonl')
    stream = path.open('xb')
    stop = threading.Event()
    overlaps, errors = [], []

    def observe_once():
        observation = {'observed_utc': datetime.now(timezone.utc).isoformat()}
        try:
            observation.update(classify(scan()))
            if observation['busy']: overlaps.append(observation)
        except Exception as error:
            observation['error'] = repr(error); errors.append(observation)
        stream.write((json.dumps(observation) + '\n').encode('utf-8')); stream.flush()

    def observe():
        while True:
            observe_once()
            if stop.wait(1): return

    thread = threading.Thread(target=observe, name='gc-build-activity-watch', daemon=True)
    thread.start()

    def finish():
        stop.set(); thread.join(timeout=12)
        assert not thread.is_alive(), 'Activity watcher did not stop'
        observe_once() # Check cumulative worker activity through phase completion.
        stream.close()
        valid = not overlaps and not errors
        phase_row['external_process_watch'] = {'log': path.name,
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'poll_interval_seconds': 1,
            'dormant_worker_admission': _admission, 'overlaps': overlaps, 'scanner_errors': errors,
            'measurement_valid': valid,
            'scope': 'All observed build/test tools invalidate timing except the exact admitted cached worker while its cumulative CPU counters and identity remain unchanged and it has no build parent or new child.',
            'limit': 'New tools entirely between samples may be missed; the admitted worker CPU counters are cumulative and never reset during this manager.'}
        return valid
    return finish
