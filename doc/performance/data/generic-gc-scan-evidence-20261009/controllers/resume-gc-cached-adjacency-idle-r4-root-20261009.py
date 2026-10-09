"""Retain the preflight refusal; wait for other projects before identical timing."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
invalid = DATA / 'gc-generic-cycles-performance-r4-20261009.json'
assert sha(invalid) == 'a59912f30a1c2c0439cbe1b5069e4d1123d02fc21945cabe100e8c5731961f14'
prior = json.loads(invalid.read_bytes())
assert prior['terminal'] and prior['hashes_unchanged'] and not prior['phases']
assert all(sha(p) == h for p, h in prior['pins_before'].items())
original = ROOT / 'scratch/performance/validate-gc-generic-cycles-performance-r4-root-20261009.py'
target = ROOT / 'scratch/performance/validate-gc-generic-cycles-performance-r4-idle-root-20261009.py'
assert not target.exists()
target.write_text(original.read_text().replace("PREFIX = 'gc-generic-cycles-performance-r4-20261009'",
    "PREFIX = 'gc-generic-cycles-performance-r4-idle-20261009'"), encoding='utf-8', newline='\n')
wait_log = DATA / 'gc-generic-cycles-performance-r4-idle-wait-20261009.jsonl'
assert not wait_log.exists()
tools = {'cl.exe', 'link.exe', 'ninja.exe', 'msbuild.exe', 'cmake.exe', 'ctest.exe', 'nmake.exe', 'clang-cl.exe', 'lld-link.exe'}
deadline = time.monotonic() + 1200
with wait_log.open('x', encoding='utf-8', newline='\n') as stream:
    while True:
        raw = subprocess.check_output(['powershell', '-NoProfile', '-Command',
            'Get-CimInstance Win32_Process | Select-Object Name,ProcessId,ParentProcessId,CommandLine | ConvertTo-Json -Compress'])
        rows = json.loads(raw.decode('utf-8-sig') or '[]')
        if isinstance(rows, dict): rows = [rows]
        busy = [p for p in rows if p['ProcessId'] != __import__('os').getpid() and
            (p['Name'].lower() in tools or p['Name'].lower().startswith(('python', 'xlang3')))]
        stream.write(json.dumps({'time_unix': time.time(), 'busy': busy}) + '\n'); stream.flush()
        if not busy: break
        print('Waiting for actual external processes:', [(p['Name'], p['ProcessId']) for p in busy], flush=True)
        if time.monotonic() >= deadline: raise TimeoutError('External tools still present; no measurements launched')
        time.sleep(30)
assert all(sha(p) == h for p, h in prior['pins_before'].items())
print('External processes finished; launching unchanged R4 timing protocol.', flush=True)
raise SystemExit(subprocess.call([sys.executable, str(target)], cwd=ROOT))
