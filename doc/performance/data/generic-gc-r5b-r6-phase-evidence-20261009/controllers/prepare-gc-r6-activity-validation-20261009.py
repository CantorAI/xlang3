"""Use the same cumulative activity guard for the new orphaned worker identity."""
import hashlib
import json
from pathlib import Path
from datetime import datetime, timezone

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
original = ROOT / 'scratch/performance/validate-gc-generic-cycles-performance-r6-root-20261009.py'
old_waiter = ROOT / 'scratch/performance/wait-gc-flat-adjacency-validation-r6-20261009.py'
assert sha(original) == 'c157bdaeb69314e520c19cad6507ae9a96176a5dec45c6d99614f8d84b07984a'
assert sha(old_waiter) == '2a322b8f3d67e698168ae8df767a981e532b6c8e0f89c9545d53db846d606e0a'
assert not (DATA / 'gc-generic-cycles-performance-r6-20261009.json').exists()
log = DATA / 'gc-generic-cycles-performance-r6-idle-wait-20261009.jsonl'
receipt = DATA / 'gc-generic-cycles-performance-r6-pre-timing-wait-stopped-20261009.json'
assert not receipt.exists()
receipt.write_text(json.dumps({'terminal': True, 'status': 'owned_pre_timing_wait_stopped',
    'owned_waiter_pid': 11968, 'stopped_utc': datetime.now(timezone.utc).isoformat(),
    'performance_phases_started': 0, 'log': log.name, 'log_sha256': sha(log),
    'reason': 'Old cached worker 34420 exited; current worker 18252 has exited parent. Stop only owned waiter to select the current identity for the unchanged cumulative CPU activity admission. No foreign process stopped.',
    'manager_sha256': sha(original), 'waiter_sha256': sha(old_waiter)}, indent=2) + '\n', encoding='utf-8', newline='\n')
manager = ROOT / 'scratch/performance/validate-gc-generic-cycles-performance-r6-activity-root-20261009.py'
assert not manager.exists()
source = original.read_text().replace('watcher.admit_dormant_worker(34420)', 'watcher.admit_dormant_worker(18252)')
source = source.replace("PREFIX = 'gc-generic-cycles-performance-r6-20261009'", "PREFIX = 'gc-generic-cycles-performance-r6-activity-20261009'")
manager.write_text(source, encoding='utf-8', newline='\n')
waiter = ROOT / 'scratch/performance/wait-gc-flat-adjacency-r6-activity-validation-20261009.py'
assert not waiter.exists()
source = old_waiter.read_text().replace('watcher.admit_dormant_worker(34420)', 'watcher.admit_dormant_worker(18252)')
source = source.replace('validate-gc-generic-cycles-performance-r6-root-', 'validate-gc-generic-cycles-performance-r6-activity-root-')
source = source.replace('gc-generic-cycles-performance-r6-idle-wait-', 'gc-generic-cycles-performance-r6-activity-idle-wait-')
waiter.write_text(source, encoding='utf-8', newline='\n')
print('manager', sha(manager))
print('waiter', sha(waiter))
