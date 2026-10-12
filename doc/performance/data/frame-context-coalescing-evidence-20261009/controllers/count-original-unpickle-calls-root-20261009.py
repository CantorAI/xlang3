"""Capture call-count diagnostics serially, with exact current source identity."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
CP = Path('C:/Python/Python314/python.exe')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CHILD = ROOT / 'scratch/performance/count-original-unpickle-calls-child-20261009.py'
CORRECT = DATA / 'trace-disable-local-events-correctness-20261009.json'
ORIGINAL = Path('C:/Python/Python314/Lib/site-packages/pyperformance/data-files/benchmarks/bm_pickle/run_benchmark.py')
PREFIX = 'original-unpickle-call-counts-20261009'
OUT = DATA / (PREFIX + '.json')
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated
assert sha(CORRECT) == '617a0841153d9f7e9620c7b23fb5c00ab55ad4e5c81b930a792e7ec773dc3e98'
correct = json.loads(CORRECT.read_bytes())
pins = {str(ROOT / p): h for p, h in correct['source_sha256'].items()}
pins.update({str(RELEASE / p): h for p, h in correct['release_sha256'].items()})
for p in (CP, CP.with_name('python314.dll'), CORRECT, CHILD, ORIGINAL, CP.parent / 'Lib/pickle.py', CP.parent / 'Lib/_pydatetime.py', Path(__file__)):
    pins[str(p)] = sha(p)
assert all(sha(p) == h for p, h in pins.items()) and not any(DATA.glob(PREFIX + '*'))
env = os.environ.copy()
for key in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE', 'PYTHONHOME'):
    env.pop(key, None)
env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
record = {'status': 'running', 'terminal': False, 'diagnostic_only': True, 'timing_score': False,
    'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
    'pins_before': pins, 'phases': []}
def save():
    OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
try:
    for label, exe, extra in (('cpython3147', CP, ['-I']), ('xlang3-current', RELEASE / 'xlang3.exe', [])):
        stdout, stderr = DATA / (PREFIX + '-' + label + '.stdout.log'), DATA / (PREFIX + '-' + label + '.stderr.log')
        command = [str(exe), *extra, str(CHILD)]
        with stdout.open('xb') as out, stderr.open('xb') as err:
            child = subprocess.Popen(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                code = child.wait(timeout=60)
            finally:
                if child.poll() is None:
                    subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=10)
                    child.wait(timeout=10)
        row = {'label': label, 'command': command, 'exit_code': code, 'stdout': stdout.name,
            'stdout_sha256': sha(stdout), 'stderr': stderr.name, 'stderr_sha256': sha(stderr)}
        record['phases'].append(row)
        save()
        assert code == 0, stderr.read_text(errors='replace')[-2500:]
        result = json.loads(stdout.read_text(encoding='utf-8'))
        assert result['protocol'] == 5 and result['loads_per_original_body'] == 60 and not result['timings_recorded']
        row['result'] = result
        save()
        print(label, 'PASS', 'Python calls', sum(r['count'] for r in result['python_calls']),
            'top', result['python_calls'][:5], 'native top', result['native_calls'][:5], flush=True)
    record['status'] = 'call_counts_recorded'
except BaseException as error:
    record.update(status='call_count_capture_failed', error=repr(error))
finally:
    record['terminal'] = True
    record['pins_after'] = {p: sha(p) for p in pins}
    record['hashes_unchanged'] = record['pins_after'] == pins
    if not record['hashes_unchanged']: record['status'] = 'invalid_hash_drift'
    save()
print(record['status'], sha(OUT), record.get('error'), flush=True)
