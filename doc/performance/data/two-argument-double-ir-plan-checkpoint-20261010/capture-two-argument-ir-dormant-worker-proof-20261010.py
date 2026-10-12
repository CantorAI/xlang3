"""Two raw identity/CPU observations; this does not authorize a benchmark launch."""
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
HELPER = ROOT / 'scratch/performance/gc-phase-dormant-msbuild-activity-watch-20261009.py'
PREFIX = 'two-argument-ir-known-dormant-worker-12776-20261010'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated
assert sha(HELPER) == '6dea8abf4f66ff0d2a95830e4c560202359c57502f0f635ecf9e1dda5494bd48'
assert not any(DATA.glob(PREFIX + '*'))
spec = importlib.util.spec_from_file_location('numeric_known_node_proof', HELPER)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
refusal_path = DATA / 'two-argument-ir-raytrace-paired-r3-20261010.json'
assert sha(refusal_path) == '9be2067442b3fc4436359af83a36ef722d3783cb59b89d4b2919913a52216051'
refusal = json.loads(refusal_path.read_bytes())
original = next(p for p in refusal['phases'][0]['pre_idle']['busy'] if p['ProcessId'] == 12776)
observations, workers, children_sets = [], [], []
for index in (1, 2):
    if index == 2:
        time.sleep(2.05)
    raw = subprocess.check_output(['powershell', '-NoProfile', '-Command', helper.SCAN], timeout=10)
    raw_path = DATA / (PREFIX + '-snapshot-' + str(index) + '.json')
    with raw_path.open('xb') as stream:
        stream.write(raw)
    parsed = json.loads(raw.decode('utf-8-sig'))
    rows = parsed if isinstance(parsed, list) else [parsed]
    worker = next(p for p in rows if p['ProcessId'] == 12776)
    assert all(k in worker and worker[k] is not None for k in helper.FIELDS)
    assert all(worker[k] == original[k] for k in helper.FIELDS)
    assert worker['Name'].lower() == 'msbuild.exe' and worker['ParentProcessId'] == 34884
    assert '/nodemode:1' in worker['CommandLine'] and '/nodeReuse:true' in worker['CommandLine']
    assert int(worker['KernelModeTime']) >= 0 and int(worker['UserModeTime']) >= 0
    assert not any(p['ProcessId'] == worker['ParentProcessId'] for p in rows)
    children = [p for p in rows if p['ParentProcessId'] == worker['ProcessId']]
    assert all(p['Name'].lower() == 'conhost.exe' for p in children)
    assert all(all(k in p and p[k] is not None for k in helper.FIELDS) for p in children)
    workers.append(worker)
    children_sets.append(children)
    observations.append(dict(observed_utc=datetime.now(timezone.utc).isoformat(),
        raw_path=str(raw_path), raw_sha256=sha(raw_path)))
assert workers[0] == workers[1] and children_sets[0] == children_sets[1]
assert (datetime.fromisoformat(observations[1]['observed_utc']) -
        datetime.fromisoformat(observations[0]['observed_utc'])).total_seconds() >= 2
proof = dict(schema='numeric-ir-dormant-msbuild-proof-v1', terminal=True, passed=True,
    helper_sha256=sha(HELPER), worker=workers[0], children=children_sets[0], observations=observations,
    controller_sha256=sha(__file__), refusal_receipt_sha256=sha(refusal_path),
    scope='Only this orphan reusable worker identity and unchanged cumulative CPU; all other active jobs remain forbidden. Not a machine-idle or launch certificate.')
path = DATA / (PREFIX + '-proof.json')
with path.open('xb') as stream:
    stream.write((json.dumps(proof, indent=2) + '\n').encode())
print('Exact dormant-worker proof', sha(path), flush=True)
