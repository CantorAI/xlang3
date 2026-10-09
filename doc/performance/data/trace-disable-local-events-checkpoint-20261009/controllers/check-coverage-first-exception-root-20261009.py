"""Untimed first-error probe; not an official Coverage benchmark."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
DATA = ROOT / 'doc/performance/data'
PREFIX = 'coverage-first-exception-untimed-20261009'
OUT = DATA / (PREFIX + '.json')
CHILD = ROOT / 'scratch/performance/coverage-untimed-first-exception-child-20261009.py'
CORRECT = DATA / 'nested-trace-setting-correctness-r2-20261009.json'
SITE = ROOT / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7)
assert sha(CORRECT) == 'de6a31342817c406d103cc9811fc5d01f82e036bf4cb6c0495ac1e3e1743619b'
correct = json.loads(CORRECT.read_bytes())
pins = {str(ROOT / p): h for p, h in correct['source_sha256'].items()}
pins.update({str(RELEASE / p): h for p, h in correct['release_sha256'].items()})
for p in (CP, CP.with_name('python314.dll'), CHILD, CORRECT, Path(__file__)):
    pins[str(p)] = sha(p)
for p in (SITE / 'coverage').rglob('*'):
    if p.is_file() and p.suffix.lower() in ('.py', '.pyd'):
        pins[str(p)] = sha(p)
assert all(sha(p) == h for p, h in pins.items()) and not any(DATA.glob(PREFIX + '*'))
env = os.environ.copy()
for key in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE', 'PYTHONHOME'):
    env.pop(key, None)
env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
record = {'terminal': False, 'timed': False, 'status': 'probing', 'phases': [], 'pins_before': pins,
    'scope': 'One fibonacci(3) operation under default Coverage, with a diagnostic top-level excepthook. No pyperf timing/calibration, replacement library code or benchmark success claim. CPython may select its native tracer while XLang3 uses Python; backend equivalence is not claimed.'}
def save():
    OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
try:
    for label, exe, extra in (('cpython3147', CP, ['-I']), ('xlang3-current', RELEASE / 'xlang3.exe', [])):
        command = [str(exe), *extra, str(CHILD)]
        stdout, stderr = DATA / (PREFIX + '-' + label + '.stdout.log'), DATA / (PREFIX + '-' + label + '.stderr.log')
        local = dict(env, PATH=str(exe.parent) + os.pathsep + env.get('PATH', ''))
        with stdout.open('xb') as out, stderr.open('xb') as err:
            child = subprocess.Popen(command, cwd=ROOT, env=local, stdin=subprocess.DEVNULL,
                stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                code = child.wait(timeout=60)
            finally:
                if child.poll() is None:
                    subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=10)
                    child.wait(timeout=10)
        text = stdout.read_text(encoding='utf-8', errors='replace')
        record['phases'].append({'label': label, 'command': command, 'exit_code': code,
            'stdout': stdout.name, 'stdout_sha256': sha(stdout), 'stderr': stderr.name, 'stderr_sha256': sha(stderr),
            'semantic_pass': code == 0 and 'PASS 2 TRACE_DISABLED True' in text})
        save()
        print(label, 'exit', code, text.strip(), flush=True)
    record['status'] = 'minimal_operation_passed' if all(p['semantic_pass'] for p in record['phases']) else 'minimal_operation_failed'
except BaseException as error:
    record.update(status='probe_error', error=repr(error))
finally:
    record['terminal'] = True
    record['pins_after'] = {p: sha(p) for p in pins}
    record['hashes_unchanged'] = record['pins_after'] == pins
    if not record['hashes_unchanged']: record['status'] = 'invalid_hash_drift'
    save()
print(record['status'], sha(OUT), flush=True)
