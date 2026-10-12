"""Authenticate the proposed constructor fixture on CPython 3.14.7 first."""
import ast
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
CP = Path('C:/Python/Python314/python.exe')
DATA = ROOT / 'doc/performance/data'
SOURCE = ROOT / 'scratch/performance/python-new-vm-continuation-r2-proposed-20261009-fixture.py'
EXPECTED = SOURCE.with_name('python-new-vm-continuation-r2-proposed-20261009-expected.out')
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7)
assert sys.flags.isolated and not sys.flags.optimize
assert sha(SOURCE) == '095b1d0b3cf8b38133efa781e911c4025d380e0af53bf2761940517680828f32'
assert sha(EXPECTED) == '8b89241811a7b183c32134f75928729322466dab8f6c2ae27c57acda3edb27d8'
ast.parse(SOURCE.read_bytes())
prefix = 'python-new-vm-continuation-cpython-reference-20261009'
assert not any(DATA.glob(prefix + '*'))
before = {str(p): sha(p) for p in (SOURCE, EXPECTED, CP, CP.with_name('python314.dll'), Path(__file__))}
command = [str(CP), '-I', str(SOURCE)]
child = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=30)
stdout, stderr = DATA / (prefix + '.stdout.log'), DATA / (prefix + '.stderr.log')
stdout.write_bytes(child.stdout)
stderr.write_bytes(child.stderr)
normalize = lambda b: b.replace(b'\r\n', b'\n')
passed = child.returncode == 0 and not child.stderr and normalize(child.stdout) == normalize(EXPECTED.read_bytes())
record = dict(status='cpython_reference_passed' if passed else 'cpython_reference_failed', terminal=True,
              passed=passed, exit_code=child.returncode, command=command, version_info=list(sys.version_info[:3]),
              hashes_before=before, hashes_after={p: sha(Path(p)) for p in before},
              stdout_log=stdout.name, stdout_sha256=sha(stdout), stderr_log=stderr.name, stderr_sha256=sha(stderr),
              expected_groups=8, actual_stdout_groups=len(child.stdout.splitlines()), timed=False, engine_changes=False)
record['hashes_unchanged'] = record['hashes_before'] == record['hashes_after']
out = DATA / (prefix + '.json')
out.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print(record['status'], 'groups', record['actual_stdout_groups'], 'receipt', sha(out))
if child.stderr:
    print(child.stderr.decode('utf-8', errors='replace'))
raise SystemExit(0 if passed and record['hashes_unchanged'] else 1)
