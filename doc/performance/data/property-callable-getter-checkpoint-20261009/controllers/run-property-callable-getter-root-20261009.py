"""Untimed root-owned four-child property dispatch diagnostic."""
import ast
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
SOURCE = ROOT / 'scratch/performance/property-callable-getter-reproducer-proposed-20261009.py'
PROOF = ROOT / 'scratch/performance/property-callable-getter-reproducer-provenance-proposed-20261009.json'
VALIDATION = DATA / 'triple-string-closing-comment-validation-20261008.json'
OUT = DATA / 'property-callable-getter-reproducer-20261009.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7)
assert sys.flags.isolated == 1 and sys.flags.optimize == 0 and not OUT.exists()
assert sha(SOURCE) == '0315386935d810fbfefaf55556777ab5f4fa1260fd4bbe87e7f3484cadcf04b5'
assert sha(PROOF) == '64ab0fa61fd72c1f3197db0d991a73cd5b79daeb8c36d07eadee9617c674f052'
assert sha(VALIDATION) == '05ffba4d1786c93448812db6ba22debc5f1c4be62ba6571ead209e49a234777a'
ast.parse(SOURCE.read_bytes())
v = json.loads(VALIDATION.read_bytes())
before = {str(ROOT / p): h for p, h in (v['source_sha256'] | v['binaries_sha256']).items()}
for p in [Path(__file__), SOURCE, PROOF, VALIDATION, CP]:
    before[str(p)] = sha(p)
assert all(sha(p) == h for p, h in before.items())
record = dict(status='preflight', terminal=False, diagnostic_only=True, scored=False,
              hashes_before=before, phases=[])
def save():
    OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
env = os.environ.copy()
env['PATH'] = str(RELEASE) + os.pathsep + env.get('PATH', '')
env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
for key in ['PYTHONPATH', 'PYTHONOPTIMIZE', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING']:
    env.pop(key, None)
try:
    for case in ['python', 'operator']:
        for runtime, command in [('cpython3147', [str(CP), '-I', str(SOURCE), case]),
                                 ('xlang3', [str(RELEASE / 'xlang3.exe'), str(SOURCE), case])]:
            key = 'property-callable-getter-20261009-' + case + '-' + runtime
            stdout, stderr = DATA / (key + '.stdout.log'), DATA / (key + '.stderr.log')
            row = dict(case=case, runtime=runtime, command=command, timed=False)
            record['phases'].append(row)
            save()
            with stdout.open('xb') as out, stderr.open('xb') as err:
                child = subprocess.run(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                                       stdout=out, stderr=err, timeout=30,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            row.update(exit_code=child.returncode, stdout_log=stdout.name, stdout_sha256=sha(stdout),
                       stderr_log=stderr.name, stderr_sha256=sha(stderr),
                       stdout=stdout.read_text(errors='replace'), stderr=stderr.read_text(errors='replace'))
            if runtime == 'cpython3147':
                assert child.returncode == 0 and 'PASS ordinary-attribute' in row['stdout']
    record['status'] = 'terminal_diagnostic_collected'
except BaseException as error:
    record.update(status='terminal_failed_diagnostic', error=repr(error))
finally:
    record['hashes_after'] = {p: sha(p) for p in before}
    record['hashes_unchanged'] = record['hashes_after'] == before
    if not record['hashes_unchanged']:
        record['status'] = 'terminal_invalid_hash_drift'
    record['terminal'] = True
    save()
print(record['status'], 'receipt_sha256', sha(OUT))
for row in record['phases']:
    print(row['case'], row['runtime'], row.get('exit_code'), row.get('stdout'), row.get('stderr'))
raise SystemExit(0 if record['status'] == 'terminal_diagnostic_collected' and record['hashes_unchanged'] else 1)
