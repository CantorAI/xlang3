"""Run a frozen constructor fixture first on CPython, then accepted XLang3."""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
CP = Path('C:/Python/Python314/python.exe')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
MANIFEST = ROOT / 'build-repro/controls/lambda-eager-comprehension-capture-accepted-r5-20261009/preserved-release-provenance.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
parser = argparse.ArgumentParser()
parser.add_argument('--proposal', required=True)
parser.add_argument('--proposal-sha256', required=True)
parser.add_argument('--prefix', required=True)
parser.add_argument('--fixture-group', choices=('main', 'completion-finalizer'), default='main')
args = parser.parse_args()
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7)
assert sys.flags.isolated and not sys.flags.optimize
assert '/' not in args.prefix and '\\' not in args.prefix
assert not any(DATA.glob(args.prefix + '*'))
proof_path = (ROOT / args.proposal).resolve()
proof_path.relative_to(ROOT.resolve())
assert sha(proof_path) == args.proposal_sha256
assert sha(MANIFEST) == '9271314856ede7f173396b2f39449a01d785f61a1579ce5f93c7786a2961dfad'
proof, saved = json.loads(proof_path.read_bytes()), json.loads(MANIFEST.read_bytes())
fixture_key, expected_key = ('fixture', 'expected') if args.fixture_group == 'main' else (
    'completion_finalizer_fixture', 'completion_finalizer_expected')
source, expected = ((ROOT / proof[n]).resolve() for n in (fixture_key, expected_key))
for p in (source, expected):
    p.relative_to(ROOT.resolve())
assert sha(source) == proof[fixture_key + '_sha256'] and sha(expected) == proof[expected_key + '_sha256']
ast.parse(source.read_bytes())
assert all(sha(ROOT / n) == h for n, h in saved['source_snapshot_sha256'].items())
assert all(sha(RELEASE / n) == h for n, h in saved['files_sha256'].items())
normalize = lambda b: b.replace(b'\r\n', b'\n')
expected_bytes = normalize(expected.read_bytes())
groups = len(expected_bytes.splitlines())
assert groups >= (8 if args.fixture_group == 'main' else 1)
common = {str(p): sha(p) for p in (source, expected, CP, CP.with_name('python314.dll'), proof_path, MANIFEST, Path(__file__))}
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
receipts = []
for label, command in (
    ('cpython-reference', [str(CP), '-I', str(source)]),
    ('accepted-xlang3-reference', [str(RELEASE / 'xlang3.exe'), str(source)]),
):
    before = common | {str(RELEASE / n): h for n, h in saved['files_sha256'].items()}
    child = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, timeout=60)
    stdout, stderr = (DATA / (args.prefix + '-' + label + '.' + suffix) for suffix in ('stdout.log', 'stderr.log'))
    stdout.write_bytes(child.stdout)
    stderr.write_bytes(child.stderr)
    after = {p: sha(Path(p)) for p in before}
    passed = child.returncode == 0 and not child.stderr and normalize(child.stdout) == expected_bytes
    record = dict(status=label + ('_passed' if passed else '_failed'), terminal=True,
                  passed=passed, exit_code=child.returncode, command=command,
                  version_info=list(sys.version_info[:3]), hashes_before=before,
                  hashes_after=after, hashes_unchanged=before == after,
                  stdout_log=stdout.name, stdout_sha256=sha(stdout),
                  stderr_log=stderr.name, stderr_sha256=sha(stderr),
                  expected_groups=groups, actual_stdout_groups=len(child.stdout.splitlines()),
                  fixture_group=args.fixture_group, timed=False, engine_changes=False)
    out = DATA / (args.prefix + '-' + label + '.json')
    out.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(record['status'], 'groups', record['actual_stdout_groups'], 'receipt', sha(out))
    receipts.append(str(out.relative_to(ROOT)))
    if not passed or not record['hashes_unchanged']:
        if child.stderr:
            print(child.stderr.decode('utf-8', errors='replace'))
        raise SystemExit(1)
assert all(sha(ROOT / n) == h for n, h in saved['source_snapshot_sha256'].items())
print('Both untimed references passed; engine and release unchanged.', json.dumps(receipts))
