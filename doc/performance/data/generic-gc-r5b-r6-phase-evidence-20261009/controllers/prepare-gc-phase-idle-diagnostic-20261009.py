"""Retain zero-phase refusal; attribute only after proving the new cached worker dormant."""
import hashlib
import json
from pathlib import Path

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
prior = DATA / 'gc-generic-cycles-phase-diagnostic-20261009.json'
record = json.loads(prior.read_bytes())
assert record['terminal'] and record['status'] == 'diagnostic_failed' and not record['phases']
original_watch = ROOT / 'scratch/performance/gc-dormant-msbuild-activity-watch-20261009.py'
assert sha(original_watch) == '016539435a7182d77232cd9129bba474deb42f0fecfd718c505779b721b28f86'
watch = ROOT / 'scratch/performance/gc-phase-dormant-msbuild-activity-watch-20261009.py'
assert not watch.exists()
source = original_watch.read_text().replace('def admit_dormant_worker():', 'def admit_dormant_worker(worker_pid):')
source = source.replace("p['ProcessId'] == 33636", "p['ProcessId'] == worker_pid")
watch.write_text(source, encoding='utf-8', newline='\n')
original = ROOT / 'scratch/performance/run-gc-phase-diagnostic-20261009.py'
target = ROOT / 'scratch/performance/run-gc-phase-idle-diagnostic-20261009.py'
assert not target.exists()
source = original.read_text().replace('import hashlib', 'import importlib.util\nimport hashlib', 1)
source = source.replace("receipt = DATA / 'gc-generic-cycles-phase-diagnostic-20261009.json'", "receipt = DATA / 'gc-generic-cycles-phase-diagnostic-idle-20261009.json'")
source = source.replace("('gc-generic-cycles-phase-diagnostic-' + name", "('gc-generic-cycles-phase-diagnostic-idle-' + name")
start = source.index('def idle():')
end = source.index('save()\ntry:', start)
source = source[:start] + '''spec = importlib.util.spec_from_file_location('phase_activity_watch', ROOT / 'scratch/performance/gc-phase-dormant-msbuild-activity-watch-20261009.py')
watcher = importlib.util.module_from_spec(spec); spec.loader.exec_module(watcher)
record['dormant_worker_admission'] = watcher.admit_dormant_worker(34420)
record['previous_zero_phase_refusal_sha256'] = sha(DATA / 'gc-generic-cycles-phase-diagnostic-20261009.json')
record['watcher_sha256'] = sha(spec.origin)
pins[spec.origin] = sha(spec.origin)
def idle():
    observation = watcher.idle_snapshot()
    assert not observation['busy'], observation['busy']
''' + source[end:]
before = '''        with stdout.open('xb') as out, stderr.open('xb') as err:
            result = subprocess.run([str(RELEASE / 'xlang3.exe'), str(script)], cwd=ROOT, env=env,
                stdout=out, stderr=err, timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)'''
after = '''        phase_row = {'name': name}
        finish = watcher.start_timing_process_watch('gc-generic-cycles-phase-diagnostic-idle-20261009', name, phase_row)
        try:
            with stdout.open('xb') as out, stderr.open('xb') as err:
                result = subprocess.run([str(RELEASE / 'xlang3.exe'), str(script)], cwd=ROOT, env=env,
                    stdout=out, stderr=err, timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)
        finally:
            valid = finish()
        assert valid, phase_row'''
assert source.count(before) == 1
source = source.replace(before, after)
source = source.replace("phases.append({'name': name, 'exit_code': result.returncode,", "phases.append({**phase_row, 'exit_code': result.returncode,")
target.write_text(source, encoding='utf-8', newline='\n')
print(sha(target))
