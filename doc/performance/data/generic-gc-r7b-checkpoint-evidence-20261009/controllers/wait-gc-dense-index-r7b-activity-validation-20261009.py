"""Wait for actual build/test inactivity, then execute the frozen manager in this PID."""
import hashlib
import importlib.util
import json
from pathlib import Path
import runpy
import time

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
app = json.loads((DATA / 'gc-generic-cycles-applied-source-r7b-20261009.json').read_bytes())
correct = json.loads((DATA / 'gc-generic-cycles-correctness-r7b-20261009.json').read_bytes())
assert correct['terminal'] and correct['correctness_passed'] and correct['sources_unchanged']
release = ROOT / 'build-repro/main-verify-20261006/Release'
pins = {str(ROOT / p): h for p, h in app['source_sha256'].items()}
pins.update({str(release / p): h for p, h in correct['release_sha256'].items()})
watch_path = ROOT / 'scratch/performance/gc-phase-dormant-msbuild-activity-watch-20261009.py'
manager = ROOT / 'scratch/performance/validate-gc-generic-cycles-performance-r7b-activity-root-20261009.py'
pins[str(watch_path)] = sha(watch_path); pins[str(manager)] = sha(manager)
spec = importlib.util.spec_from_file_location('r6_readiness_watch', watch_path)
watcher = importlib.util.module_from_spec(spec); spec.loader.exec_module(watcher)
log = DATA / 'gc-generic-cycles-performance-r7b-activity-idle-wait-20261009.jsonl'
assert not log.exists()
deadline = time.monotonic() + 1200
with log.open('x', encoding='utf-8', newline='\n') as stream:
    while True:
        assert all(sha(p) == h for p, h in pins.items())
        row = {'time_unix': time.time()}
        try:
            row['dormant_admission'] = watcher.admit_dormant_worker(18252)
            row.update(watcher.idle_snapshot())
        except Exception as error:
            row['admission_error'] = repr(error)
            row.update(watcher.classify(watcher.scan(), include_runtimes=True))
        stream.write(json.dumps(row) + '\n'); stream.flush()
        if not row.get('busy') and 'admission_error' not in row: break
        print('Waiting for real external build/test activity:', [(p['Name'], p['ProcessId']) for p in row.get('busy', [])], row.get('admission_error', ''), flush=True)
        if time.monotonic() >= deadline: raise TimeoutError('No uncontended window; no benchmark launched')
        time.sleep(30)
assert all(sha(p) == h for p, h in pins.items())
print('Host ready; launching unchanged gate and official GC cases in the same manager PID.', flush=True)
runpy.run_path(str(manager), run_name='__main__')
