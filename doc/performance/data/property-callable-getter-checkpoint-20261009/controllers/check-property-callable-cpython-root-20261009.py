"""Capture exact CPython 3.14.7 output for the frozen semantic fixture."""
import ast
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
PROOF = ROOT / 'scratch/performance/property-callable-getter-proposed-20261009-provenance.json'
OUT = DATA / 'property-callable-getter-cpython-reference-20261009.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7)
assert sys.flags.isolated and not sys.flags.optimize and not OUT.exists()
assert sha(PROOF) == 'aed4a764163c3d5d22e876a7c0a0bfc9883c105ed8cc2042649df5cbfd85ef9a'
proof = json.loads(PROOF.read_bytes())
source = ROOT / proof['mapping']['tests/fixtures/core/property_callable_getter.py']
expected = ROOT / proof['mapping']['tests/fixtures/expected/property_callable_getter.out']
assert sha(source) == proof['candidate_source_sha256']['tests/fixtures/core/property_callable_getter.py']
assert sha(expected) == proof['candidate_source_sha256']['tests/fixtures/expected/property_callable_getter.out']
ast.parse(source.read_bytes())
ast.parse((ROOT / proof['mapping']['tests/run_fixtures.py']).read_bytes())
parent_path = ROOT / 'build-repro/controls/property-callable-getter-parent-20261009/preserved-release-provenance.json'
assert sha(parent_path) == '608aadc303532321de8ba8cf464ce7d341ba6366373d386ab2230e865c9f6573'
parent = json.loads(parent_path.read_bytes())
before = {str(ROOT / p): h for p, h in parent['source_snapshot_sha256'].items()}
for p in [Path(__file__), PROOF, source, expected, CP, CP.parent / 'python314.dll', parent_path]:
    before[str(p)] = sha(p)
assert all(sha(p) == h for p, h in before.items())
stdout, stderr = DATA / 'property-callable-getter-cpython-reference-20261009.stdout.log', DATA / 'property-callable-getter-cpython-reference-20261009.stderr.log'
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
              hashes_before=before, hashes_after=after, hashes_unchanged=before == after,
              controller_sha256=sha(Path(__file__)))
OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print(record['status'], sha(OUT))
print(stdout.read_text(), stderr.read_text())
raise SystemExit(0 if passed and before == after else 1)
