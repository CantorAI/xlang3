"""Record current backend selection, without inferring historical selections."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CHILD = ROOT / 'scratch/performance/coverage-backend-untimed-child-20261009.py'
CORRECT = DATA / 'trace-disable-local-events-correctness-20261009.json'
PREFIX = 'coverage-backend-untimed-20261009'
OUT = DATA / (PREFIX + '.json')
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert sha(CORRECT) == '617a0841153d9f7e9620c7b23fb5c00ab55ad4e5c81b930a792e7ec773dc3e98'
correct = json.loads(CORRECT.read_bytes())
pins = {str(ROOT / p): h for p, h in correct['source_sha256'].items()}
pins.update({str(RELEASE / p): h for p, h in correct['release_sha256'].items()})
for p in (CP, CP.with_name('python314.dll'), CHILD, CORRECT, Path(__file__)):
    pins[str(p)] = sha(p)
assert all(sha(p) == h for p, h in pins.items()) and not any(DATA.glob(PREFIX + '*'))
env = os.environ.copy()
for k in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE', 'PYTHONHOME'):
    env.pop(k, None)
env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
record = {'terminal': False, 'timed': False, 'status': 'checking', 'phases': [], 'pins_before': pins,
    'scope': 'Current default backend selection for the same coverage package, with no override added. No benchmark or historical saved-worker backend proof.'}
for label, exe, extra in (('cpython3147', CP, ['-I']), ('xlang3-current', RELEASE / 'xlang3.exe', [])):
    stdout, stderr = DATA / (PREFIX + '-' + label + '.stdout.log'), DATA / (PREFIX + '-' + label + '.stderr.log')
    command = [str(exe), *extra, str(CHILD)]
    with stdout.open('xb') as out, stderr.open('xb') as err:
        result = subprocess.run(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
            stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW, timeout=60)
    row = {'label': label, 'command': command, 'exit_code': result.returncode, 'stdout': stdout.name,
           'stdout_sha256': sha(stdout), 'stderr': stderr.name, 'stderr_sha256': sha(stderr)}
    record['phases'].append(row)
    if result.returncode == 0:
        row['result'] = json.loads(stdout.read_text(encoding='utf-8'))
    print(label, row.get('result', stderr.read_text(errors='replace')[-1500:]), flush=True)
record['terminal'] = True
record['pins_after'] = {p: sha(p) for p in pins}
record['hashes_unchanged'] = record['pins_after'] == pins
record['status'] = 'backend_selection_recorded' if record['hashes_unchanged'] and all(p['exit_code'] == 0 for p in record['phases']) else 'backend_probe_failed'
OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
print(record['status'], sha(OUT), flush=True)
