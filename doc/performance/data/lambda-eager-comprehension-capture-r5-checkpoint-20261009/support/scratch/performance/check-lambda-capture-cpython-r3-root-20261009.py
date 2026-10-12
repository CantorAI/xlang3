"""Untimed CPython reference for the frozen eight-group compiler fixture."""
import ast
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
PROOF = ROOT / 'scratch/performance/lambda-eager-comprehension-capture-r3-proposed-20261009-provenance.json'
OUT = DATA / 'lambda-eager-comprehension-capture-cpython-reference-r3-20261009.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7)
assert sys.flags.isolated and not sys.flags.optimize and not OUT.exists()
assert sha(PROOF) == '68d024b1e32cc46801c4730fe13bdfe075d8967bb4310a104945a87d7d5c479d'
proof = json.loads(PROOF.read_bytes())
mapping = {r['target']: r for r in proof['mapping']}
source_row = mapping['tests/fixtures/core/lambda_eager_comprehension_capture.py']
expected_row = mapping['tests/fixtures/expected/lambda_eager_comprehension_capture.out']
source, expected = ROOT / source_row['source'], ROOT / expected_row['source']
assert sha(source) == source_row['sha256'] and sha(expected) == expected_row['sha256']
ast.parse(source.read_bytes())
ast.parse((ROOT / mapping['tests/run_fixtures.py']['source']).read_bytes())
parent_path = ROOT / 'build-repro/controls/lambda-eager-comprehension-capture-parent-20261009/preserved-release-provenance.json'
assert sha(parent_path) == '865ed0cfaed6e20bc47710808ebbe29cc56ed8d11426ba2e90e008490308b0e1'
parent = json.loads(parent_path.read_bytes())
before = {str(ROOT / p): h for p, h in parent['source_snapshot_sha256'].items()}
for p in [Path(__file__), PROOF, source, expected, CP, CP.parent / 'python314.dll', parent_path]:
    before[str(p)] = sha(p)
assert all(sha(p) == h for p, h in before.items())
stdout, stderr = DATA / 'lambda-eager-comprehension-capture-cpython-reference-r3-20261009.stdout.log', DATA / 'lambda-eager-comprehension-capture-cpython-reference-r3-20261009.stderr.log'
command = [str(CP), '-I', str(source)]
with stdout.open('xb') as out, stderr.open('xb') as err:
    child = subprocess.run(command, cwd=ROOT, stdin=subprocess.DEVNULL, stdout=out, stderr=err,
                           timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
passed = child.returncode == 0 and not stderr.read_bytes() and stdout.read_bytes().replace(b'\r\n', b'\n') == expected.read_bytes().replace(b'\r\n', b'\n')
after = {p: sha(p) for p in before}
record = dict(status='cpython_reference_passed' if passed else 'cpython_reference_failed', terminal=True,
              passed=bool(passed), timed=False, command=command, exit_code=child.returncode,
              stdout_log=stdout.name, stdout_sha256=sha(stdout), stderr_log=stderr.name, stderr_sha256=sha(stderr),
              expected_sha256=sha(expected), fixture_sha256=sha(source), proposal_sha256=sha(PROOF),
              parent_manifest_sha256=sha(parent_path), hashes_before=before, hashes_after=after,
              hashes_unchanged=before == after, controller_sha256=sha(Path(__file__)))
OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print(record['status'], sha(OUT))
print(stdout.read_text(), stderr.read_text())
raise SystemExit(0 if passed and before == after else 1)
