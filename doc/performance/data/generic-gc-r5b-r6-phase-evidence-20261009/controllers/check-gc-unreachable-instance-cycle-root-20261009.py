"""CP-first untimed reproduction on restored accepted Release; no speed score."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CHILD = ROOT / 'scratch/performance/gc-unreachable-instance-cycle-child-20261009.py'
RESTORED = DATA / 'frame-context-coalescing-rejected-restored-20261009.json'
OUT = DATA / 'gc-unreachable-instance-cycle-untimed-20261009.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7)
assert sha(RESTORED) == '69d0a50979c12f8cdb31d6f62e8ca625345e1ead6f2b7fbf10c7802e533668e4'
restored = json.loads(RESTORED.read_bytes())
pins = {str(ROOT / p): h for p, h in restored['restored_source_sha256'].items()}
pins.update({str(RELEASE / p): h for p, h in restored['restored_release_sha256'].items()})
for p in (CP, CP.with_name('python314.dll'), CHILD, RESTORED, Path(__file__)):
    pins[str(p)] = sha(p)
assert all(sha(p) == h for p, h in pins.items()) and not OUT.exists()
env = os.environ.copy()
for key in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE', 'PYTHONHOME', 'XLANG3_VM_OPCODE_TIMING'):
    env.pop(key, None)
env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
rows = []
for label, exe, extra in (('cpython3147', CP, ['-I']), ('xlang3-accepted', RELEASE / 'xlang3.exe', [])):
    command = [str(exe), *extra, str(CHILD)]
    result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, timeout=30,
        stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
    stdout, stderr = (DATA / ('gc-unreachable-instance-cycle-untimed-20261009-' + label + suffix) for suffix in ('.stdout.log', '.stderr.log'))
    stdout.write_bytes(result.stdout); stderr.write_bytes(result.stderr)
    row = {'label': label, 'command': command, 'exit_code': result.returncode,
        'stdout': stdout.name, 'stdout_sha256': sha(stdout), 'stderr': stderr.name, 'stderr_sha256': sha(stderr)}
    line = next(s for s in result.stdout.decode('utf-8').splitlines() if s.startswith('GC_RESULT '))
    _, count, dead_present, live_same = line.split()
    row.update(collected=int(count), unreachable_still_tracked=dead_present == 'True', reachable_preserved=live_same == 'True')
    rows.append(row)
unchanged = all(sha(p) == h for p, h in pins.items())
reproduced = rows[0]['exit_code'] == 0 and rows[0]['collected'] >= 1 and not rows[0]['unreachable_still_tracked'] and rows[1]['exit_code'] != 0 and rows[1]['collected'] == 0 and rows[1]['unreachable_still_tracked'] and all(r['reachable_preserved'] for r in rows)
record = {'terminal': True, 'status': 'unreachable_cycle_discovery_failure_reproduced' if reproduced and unchanged else 'unexpected_result',
    'timed': False, 'speed_score': False, 'phases': rows, 'pins_before': pins, 'hashes_unchanged': unchanged,
    'scope': 'Generic self-cycle with no weakref/finalizer seed; integer id does not retain the abandoned node. Separate reachable self-cycle must survive. No benchmark alteration or collection-count workaround.'}
OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({'status': record['status'], 'phases': rows, 'receipt_sha256': sha(OUT)}, indent=2))
raise SystemExit(0 if reproduced and unchanged else 1)
