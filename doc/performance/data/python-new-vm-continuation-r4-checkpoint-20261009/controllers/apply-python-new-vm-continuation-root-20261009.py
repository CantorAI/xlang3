"""Apply only an independently reviewed, CPython-verified constructor candidate.

Root supplies the frozen proposal and actual CPython receipt hashes; this
controller never builds, measures, stages, or changes the accepted controls.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CONTROL = ROOT / 'build-repro/controls/lambda-eager-comprehension-capture-accepted-r5-20261009'
MANIFEST = CONTROL / 'preserved-release-provenance.json'
HEAD = '4b1cfefc80f0bbb9e1f60c09c46d83619ded0e17'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}

def contained(name):
    p = (ROOT / name).resolve()
    p.relative_to(ROOT.resolve())
    return p

parser = argparse.ArgumentParser()
parser.add_argument('--proposal', required=True)
parser.add_argument('--proposal-sha256', required=True)
parser.add_argument('--cpython-receipt', required=True)
parser.add_argument('--cpython-receipt-sha256', required=True)
parser.add_argument('--finalizer-cpython-receipt', required=True)
parser.add_argument('--finalizer-cpython-receipt-sha256', required=True)
parser.add_argument('--out', required=True)
args = parser.parse_args()
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated and not sys.flags.optimize
proof_path, cp_path, out = map(contained, (args.proposal, args.cpython_receipt, args.out))
finalizer_cp_path = contained(args.finalizer_cpython_receipt)
assert not out.exists()
assert sha(proof_path) == args.proposal_sha256
assert sha(cp_path) == args.cpython_receipt_sha256
assert sha(finalizer_cp_path) == args.finalizer_cpython_receipt_sha256
assert sha(MANIFEST) == '9271314856ede7f173396b2f39449a01d785f61a1579ce5f93c7786a2961dfad'
proof, cp, saved = (json.loads(p.read_bytes()) for p in (proof_path, cp_path, MANIFEST))
finalizer_cp = json.loads(finalizer_cp_path.read_bytes())
assert saved['terminal'] and saved['full_validated'] and saved['correctness_passed'] and saved['fixed_gate_passed']
assert cp['terminal'] and cp['passed'] and cp['hashes_unchanged'] and cp['exit_code'] == 0
assert cp['version_info'] == [3, 14, 7]
assert finalizer_cp['terminal'] and finalizer_cp['passed'] and finalizer_cp['hashes_unchanged']
assert finalizer_cp['exit_code'] == 0 and finalizer_cp['version_info'] == [3, 14, 7]
assert proof['head'] == HEAD and proof['parent_manifest_sha256'] == sha(MANIFEST)
sources, dirty = saved['source_snapshot_sha256'], saved['tracked_dirty_sha256']
assert len(sources) == proof['source_count'] == 128
assert sources == proof['source_sha256'] == proof['raw_before_sha256']
assert all(sha(contained(n)) == h for n, h in (sources | dirty).items())
assert tree(RELEASE) == tree(CONTROL / 'Release') == saved['files_sha256']
assert tree(ROOT / 'build-repro/Release') == saved['fixed_baseline_sha256']
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == HEAD
assert not subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT)
targets = proof['candidate_source_sha256']
mapping = {r['target']: contained(r['source']) for r in proof['mapping']}
expected_targets = {
    'src/executor/xlang_vm/ops/xlang_vm_ops_call.h',
    'src/executor/xlang_vm/xlang_frame.h',
    'src/executor/xlang_vm/xlang_vm_loop.cpp',
    'src/executor/xlang_vm/xlang_vm_op_rows.h',
}
assert set(targets) == set(mapping) == expected_targets
assert all(sha(mapping[n]) == h for n, h in targets.items())
assert all(sha(contained(n)) == h for n, h in proof['input_sha256'].items())
patch = contained(proof['patch'])
assert sha(patch) == proof['patch_sha256']
subprocess.run(['git', 'apply', '--check', str(patch)], cwd=ROOT, check=True, capture_output=True)
fixture, expected = contained(proof['fixture']), contained(proof['expected'])
assert sha(fixture) == proof['fixture_sha256'] and sha(expected) == proof['expected_sha256']
assert cp['hashes_before'][str(fixture)] == sha(fixture)
assert cp['hashes_before'][str(expected)] == sha(expected)
for name, h in cp['hashes_before'].items():
    assert sha(Path(name)) == h
new_fixture = 'tests/fixtures/core/python_new_vm_continuation.py'
new_expected = 'tests/fixtures/expected/python_new_vm_continuation.out'
assert not contained(new_fixture).exists() and not contained(new_expected).exists()
finalizer_fixture, finalizer_expected = (contained(proof[n]) for n in (
    'completion_finalizer_fixture', 'completion_finalizer_expected'))
assert sha(finalizer_fixture) == proof['completion_finalizer_fixture_sha256']
assert sha(finalizer_expected) == proof['completion_finalizer_expected_sha256']
assert finalizer_cp['hashes_before'][str(finalizer_fixture)] == sha(finalizer_fixture)
assert finalizer_cp['hashes_before'][str(finalizer_expected)] == sha(finalizer_expected)
for name, h in finalizer_cp['hashes_before'].items():
    assert sha(Path(name)) == h
new_finalizer_fixture = 'tests/fixtures/core/python_new_completion_finalizer.py'
new_finalizer_expected = 'tests/fixtures/expected/python_new_completion_finalizer.out'
assert not contained(new_finalizer_fixture).exists() and not contained(new_finalizer_expected).exists()
runner_bytes = {n: contained(n).read_bytes() for n in ('tests/run_fixtures.py', 'tests/run_fixtures.ps1')}
py_anchor = b'property_callable_getter lambda_eager_comprehension_capture '
ps_anchor = b'    "lambda_eager_comprehension_capture",'
assert runner_bytes['tests/run_fixtures.py'].count(py_anchor) == 1
assert runner_bytes['tests/run_fixtures.ps1'].count(ps_anchor) == 1
assert all(b'python_new_vm_continuation' not in b for b in runner_bytes.values())
ps_eol = b'\r\n' if b'\r\n' in runner_bytes['tests/run_fixtures.ps1'] else b'\n'
runner_after = {
    'tests/run_fixtures.py': runner_bytes['tests/run_fixtures.py'].replace(
        py_anchor, py_anchor + b'python_new_vm_continuation python_new_completion_finalizer '),
    'tests/run_fixtures.ps1': runner_bytes['tests/run_fixtures.ps1'].replace(
        ps_anchor, ps_anchor + ps_eol + b'    "python_new_vm_continuation",' + ps_eol +
        b'    "python_new_completion_finalizer",'),
}
# Every input is checked before the first mutation. Copy complete reviewed
# candidate bytes; don't apply an unchecked fuzzy patch to a dirty worktree.
for n in sorted(targets):
    contained(n).write_bytes(mapping[n].read_bytes())
for n, b in runner_after.items():
    contained(n).write_bytes(b)
contained(new_fixture).write_bytes(fixture.read_bytes())
contained(new_expected).write_bytes(expected.read_bytes())
contained(new_finalizer_fixture).write_bytes(finalizer_fixture.read_bytes())
contained(new_finalizer_expected).write_bytes(finalizer_expected.read_bytes())
owned = targets | {n: sha(contained(n)) for n in runner_after} | {
    new_fixture: sha(fixture), new_expected: sha(expected),
    new_finalizer_fixture: sha(finalizer_fixture), new_finalizer_expected: sha(finalizer_expected),
}
after = sources | owned
assert len(after) == 132 and len(owned) == 10
assert all(sha(contained(n)) == h for n, h in (after | dirty).items())
assert tree(RELEASE) == tree(CONTROL / 'Release') == saved['files_sha256']
assert tree(ROOT / 'build-repro/Release') == saved['fixed_baseline_sha256']
assert not subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT)
record = dict(
    status='applied_reviewed_python_new_vm_continuation', terminal=True,
    head=HEAD, source_count=len(after), source_sha256=after, targets_sha256=owned,
    proposal=str(proof_path.relative_to(ROOT)), proposal_sha256=sha(proof_path),
    patch_sha256=sha(patch), cpython_reference=str(cp_path.relative_to(ROOT)),
    cpython_reference_sha256=sha(cp_path), parent_manifest_sha256=sha(MANIFEST),
    finalizer_cpython_reference=str(finalizer_cp_path.relative_to(ROOT)),
    finalizer_cpython_reference_sha256=sha(finalizer_cp_path),
    controller_sha256=sha(__file__), unowned_tracked_dirty_sha256=dirty,
    parent_release_preserved=True, fixed_baseline_unchanged=True,
    build_pending=True, performance_validation_pending=True,
    source_identity_limit=saved['source_identity_limit'],
)
out.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print(record['status'], 'source', len(after), 'owned', len(owned), 'receipt', sha(out))
