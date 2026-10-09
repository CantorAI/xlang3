"""Retain a stopped pre-timing wait and prepare identical benchmarks with activity evidence."""
import hashlib
import json
from pathlib import Path

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
original = ROOT / 'scratch/performance/validate-gc-generic-cycles-performance-r4-root-20261009.py'
assert sha(original) == 'f6be87aac859b7d3b77a3ea834486530630fff539717d8c11bade19faa2375e9'
assert not (DATA / 'gc-generic-cycles-performance-r4-idle-20261009.json').exists()
wait = DATA / 'gc-generic-cycles-performance-r4-idle-wait-20261009.jsonl'
stop = DATA / 'gc-generic-cycles-performance-r4-idle-wait-stopped-20261009.json'
assert not stop.exists()
stop.write_text(json.dumps({'terminal': True, 'owned_waiter_pid': 20740,
    'reason': 'Stopped only the owned pre-timing waiter after verifying it launched no timing. Other project tests finished; orphaned reusable MSBuild node remained with unchanged CPU counters.',
    'timing_phases_launched': 0, 'wait_log_sha256': sha(wait),
    'other_project_processes_terminated': False, 'controller_sha256': sha(__file__)}, indent=2) + '\n', encoding='utf-8')
watch = ROOT / 'scratch/performance/gc-dormant-msbuild-activity-watch-20261009.py'
source = original.read_text()
source = source.replace('validate-call-ex-cross-activation-constructor-resume-r3-20261008.py', watch.name)
source = source.replace('50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf', sha(watch))
source = source.replace("PREFIX = 'gc-generic-cycles-performance-r4-20261009'", "PREFIX = 'gc-generic-cycles-performance-r4-activity-20261009'")
start = source.index('        raw = subprocess.check_output', source.index('    def idle(label):'))
end = source.index('    spec = importlib.util.spec_from_file_location', start)
source = source[:start] + '''        observation = watcher.idle_snapshot()
        record['idle_guards'].append({'phase': label, **observation}); save()
        assert not observation['busy'], observation['busy']
''' + source[end:]
anchor = '    env = os.environ.copy()'
assert source.count(anchor) == 1
source = source.replace(anchor, "    record['dormant_worker_admission'] = watcher.admit_dormant_worker()\n" + anchor)
target = ROOT / 'scratch/performance/validate-gc-generic-cycles-performance-r4-activity-root-20261009.py'
assert not target.exists()
target.write_text(source, encoding='utf-8', newline='\n')
print(json.dumps({'prepared_manager': str(target), 'sha256': sha(target), 'watcher_sha256': sha(watch)}, indent=2))
