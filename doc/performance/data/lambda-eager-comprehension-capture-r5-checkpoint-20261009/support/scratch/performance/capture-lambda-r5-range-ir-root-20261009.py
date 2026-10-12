"""Capture range/cell IR through the public CLI; no benchmark or speed claim."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
OUT = DATA / 'lambda-eager-comprehension-capture-r5-range-ir-20261009.json'
IR = ROOT / 'scratch/performance/lambda-capture-r5-emitted-ir-20261009/nested_comprehension_capture'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}
assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
assert not OUT.exists() and not IR.exists()
build_path = DATA / 'lambda-eager-comprehension-capture-r5-build-terminal-20261009.json'
assert sha(build_path) == '5a18941f71560ec98879872a0039703df19ad4856cd8225e375f3a3302b41fea'
b = json.loads(build_path.read_bytes())
before = {str(ROOT / p): h for p, h in b['source_sha256'].items()}
before.update({str(RELEASE / p): h for p, h in b['binaries_sha256'].items()})
source = ROOT / 'tests/fixtures/core/nested_comprehension_capture.py'
expected = ROOT / 'tests/fixtures/expected/nested_comprehension_capture.out'
for p in [source, expected, build_path, Path(__file__)]:
    before[str(p)] = sha(p)
assert all(sha(p) == h for p, h in before.items())
env = os.environ.copy()
env['PATH'] = str(RELEASE) + os.pathsep + env.get('PATH', '')
env['XLANG3_PYTHON_LIB'] = 'C:/Python/Python314/Lib'
for k in ['PYTHONPATH', 'PYTHONHOME', 'PYTHONOPTIMIZE', 'PYTHONPYCACHEPREFIX']:
    env.pop(k, None)
command = [str(RELEASE / 'xlang3.exe'), '--dump-ir', '--debug-dir', str(IR), str(source)]
stdout = DATA / 'lambda-eager-comprehension-capture-r5-range-ir-20261009.stdout.log'
stderr = DATA / 'lambda-eager-comprehension-capture-r5-range-ir-20261009.stderr.log'
with stdout.open('xb') as o, stderr.open('xb') as e:
    child = subprocess.run(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL, stdout=o, stderr=e,
                           timeout=90, creationflags=subprocess.CREATE_NO_WINDOW)
ir_file = IR / 'nested_comprehension_capture.ir.txt'
passed = child.returncode == 0 and stdout.read_bytes().replace(b'\r\n', b'\n') == expected.read_bytes().replace(b'\r\n', b'\n') and stderr.read_bytes() == ('debug: wrote IR ' + str(ir_file) + '\n').encode() and ir_file.exists()
stable = all(sha(p) == h for p, h in before.items()) and tree(ROOT / 'build-repro/Release') == b['baseline_sha256']
record = dict(status='untimed_range_ir_captured' if passed else 'untimed_range_ir_failed', terminal=True,
              passed=passed, hashes_unchanged=stable, timed=False, performance_claim=False, command=command,
              source_sha256=b['source_sha256'], source_inventory_sha256=b['source_inventory_sha256'],
              candidate_binary_sha256=b['candidate_binary_sha256'], input_sha256=before,
              exit_code=child.returncode, stdout_sha256=sha(stdout), stderr_sha256=sha(stderr),
              emitted_files_sha256=tree(IR), build_receipt_sha256=sha(build_path))
OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print(record['status'], 'hashstable', stable, sha(OUT))
assert passed and stable
