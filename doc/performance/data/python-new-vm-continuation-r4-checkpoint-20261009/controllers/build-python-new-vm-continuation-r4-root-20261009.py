"""Build the applied VM continuation in the existing VC/Ninja Release tree."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CONTROL = ROOT / 'build-repro/controls/lambda-eager-comprehension-capture-accepted-r5-20261009'
APP = DATA / 'python-new-vm-continuation-r4-applied-source-20261009.json'
CMD = ROOT / 'scratch/performance/build-python-new-vm-continuation-r4-root-20261009.cmd'
PREFIX = 'python-new-vm-continuation-r4-build-20261009'
LOG, OUT = DATA / (PREFIX + '.log'), DATA / (PREFIX + '.json')
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated and not sys.flags.optimize
assert sha(APP) == '2c89133fbe3c3a4c0d13a1652b62362d69be1c79b07e197186682eac664cb0a8'
app = json.loads(APP.read_bytes())
manifest = CONTROL / 'preserved-release-provenance.json'
assert sha(manifest) == '9271314856ede7f173396b2f39449a01d785f61a1579ce5f93c7786a2961dfad'
saved = json.loads(manifest.read_bytes())
source = app['source_sha256'] | app['unowned_tracked_dirty_sha256']
assert len(app['source_sha256']) == 132 and len(app['targets_sha256']) == 10
assert all(sha(ROOT / n) == h for n, h in source.items())
assert tree(RELEASE) == tree(CONTROL / 'Release') == saved['files_sha256']
assert tree(ROOT / 'build-repro/Release') == saved['fixed_baseline_sha256']
assert not LOG.exists() and not OUT.exists()
command = ['cmd.exe', '/d', '/c', str(CMD)]
before = {str(p): sha(p) for p in (APP, CMD, Path(__file__), manifest)}
start = time.time()
print('Building existing Release tree with vcvars64; raw log', LOG.name, flush=True)
with LOG.open('wb') as stream:
    child = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
binaries = tree(RELEASE)
source_stable = all(sha(ROOT / n) == h for n, h in source.items())
control_stable = tree(CONTROL / 'Release') == saved['files_sha256']
baseline_stable = tree(ROOT / 'build-repro/Release') == saved['fixed_baseline_sha256']
inputs_stable = all(sha(Path(n)) == h for n, h in before.items())
passed = child.returncode == 0 and source_stable and control_stable and baseline_stable and inputs_stable
record = dict(status='build_passed' if passed else 'build_failed', terminal=True,
              passed=passed, exit_code=child.returncode, command=command,
              started_at_unix=start, finished_at_unix=time.time(),
              source_count=132, source_sha256=app['source_sha256'],
              application_sha256=sha(APP), hashes_before=before,
              hashes_after={n: sha(Path(n)) for n in before},
              source_unchanged=source_stable, accepted_control_unchanged=control_stable,
              fixed_baseline_unchanged=baseline_stable, binaries_sha256=binaries,
              binary_file_count=len(binaries), log=LOG.name, log_sha256=sha(LOG),
              exe_sha256=sha(RELEASE / 'xlang3.exe'), dll_sha256=sha(RELEASE / 'xlang3_runtime.dll'),
              timed=False, performance_validation_pending=True)
OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print(record['status'], 'exit', child.returncode, 'receipt', sha(OUT), flush=True)
raise SystemExit(0 if passed else 1)
