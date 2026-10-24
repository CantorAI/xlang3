"""Capture actual failed dictionary-comprehension IR; untimed, no score claim."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BUILD = DATA / 'lambda-eager-comprehension-capture-r4-build-terminal-20261009.json'
OUT = DATA / 'lambda-eager-comprehension-capture-r4-emitted-ir-20261009.json'
IR = ROOT / 'scratch/performance/lambda-capture-r4-emitted-ir-20261009'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}
assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
assert not OUT.exists() and not IR.exists()
assert sha(BUILD) == 'a14343ff797015d3444f40da210e927a6a1991b218d877956825754f1049ceb0'
b = json.loads(BUILD.read_bytes())
before = {str(ROOT / p): h for p, h in b['source_sha256'].items()}
before.update({str(RELEASE / p): h for p, h in b['binaries_sha256'].items()})
before[str(BUILD)] = sha(BUILD)
before[str(Path(__file__))] = sha(__file__)
assert all(sha(p) == h for p, h in before.items())
env = os.environ.copy()
env['PATH'] = str(RELEASE) + os.pathsep + env.get('PATH', '')
env['XLANG3_PYTHON_LIB'] = 'C:/Python/Python314/Lib'
for k in ['PYTHONPATH', 'PYTHONHOME', 'PYTHONOPTIMIZE', 'PYTHONPYCACHEPREFIX']:
    env.pop(k, None)
source = ROOT / 'tests/fixtures/core/lambda_eager_comprehension_capture.py'
command = [str(RELEASE / 'xlang3.exe'), '--dump-ir', '--debug-dir', str(IR), str(source)]
stdout = DATA / 'lambda-eager-comprehension-capture-r4-emitted-ir-20261009.stdout.log'
stderr = DATA / 'lambda-eager-comprehension-capture-r4-emitted-ir-20261009.stderr.log'
with stdout.open('xb') as o, stderr.open('xb') as e:
    child = subprocess.run(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL, stdout=o, stderr=e,
                           timeout=90, creationflags=subprocess.CREATE_NO_WINDOW)
stable = all(sha(p) == h for p, h in before.items()) and tree(ROOT / 'build-repro/Release') == b['baseline_sha256']
record = dict(status='untimed_failed_fixture_ir_captured', terminal=True, timed=False, performance_claim=False,
              command=command, exit_code=child.returncode, hashes_before=before, hashes_unchanged=stable,
              stdout_sha256=sha(stdout), stderr_sha256=sha(stderr), emitted_files_sha256=tree(IR),
              build_receipt_sha256=sha(BUILD))
OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print(record['status'], 'exit', child.returncode, 'hashstable', stable, 'receipt', sha(OUT))
print('Emitted files', list(record['emitted_files_sha256']))
assert child.returncode == 1 and stable and record['emitted_files_sha256']
