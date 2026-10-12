"""Retain the four valid original IR captures and fix only probe assumptions."""
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
PARENT = DATA / 'python-new-vm-continuation-r4-eligibility-20261009.json'
BUILD = DATA / 'python-new-vm-continuation-r4-build-20261009.json'
PREFIX = 'python-new-vm-continuation-r4-eligibility-r2-20261009'
DIR, OUT = DATA / PREFIX, DATA / (PREFIX + '.json')
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated and not sys.flags.optimize
assert sha(PARENT) == 'ace97cea5b00a3d53d4af7b188cb01a28eb28f9d13a510426a26b95749f7c4f5'
assert sha(BUILD) == 'd4dc98f0c23e1d2b902a6a658cc1d7211c59ca01caa8f10a4a95ffb5d67644b9'
parent, build = json.loads(PARENT.read_bytes()), json.loads(BUILD.read_bytes())
assert parent['terminal'] and parent['hashes_unchanged'] and not parent['passed']
assert tree(RELEASE) == parent['binaries_sha256'] == build['binaries_sha256']
assert all(sha(Path(n)) == h for n, h in parent['hashes_after'].items())
assert not DIR.exists() and not OUT.exists()
old_probe = ROOT / 'scratch/performance/python-new-original-unpickle-probe-root-20261009.py'
probe = ROOT / 'scratch/performance/python-new-original-unpickle-probe-r2-root-20261009.py'
assert not probe.exists()
raw = old_probe.read_bytes()
old = b"assert selected_class.__module__ == '_pydatetime'"
new = (b"assert selected_class.__module__ == 'datetime'\n"
       b"assert Path(selected_class.__new__.__code__.co_filename).resolve() == Path('C:/Python/Python314/Lib/_pydatetime.py').resolve()")
assert raw.count(old) == 1
probe.write_bytes(raw.replace(old, new))
ast.parse(probe.read_bytes())
DIR.mkdir()
before = dict(parent['hashes_after'])
before.update({str(p): sha(p) for p in (PARENT, BUILD, probe, Path(__file__))})
benchmark = Path('C:/Python/Python314/Lib/site-packages/pyperformance/data-files/benchmarks/bm_pickle/run_benchmark.py')
assert sha(benchmark) == '31c0e30be79514b45db0d4e858632eb429b0fea3edbac6463ad5f34d92f3cab8'
before[str(benchmark)] = sha(benchmark)
phases, blocks = [], {}
for row in parent['phases'][:4]:
    assert row['exit_code'] == 0 and row['ir']
    for key in ('stdout', 'stderr', 'ir'):
        assert sha(DATA / row[key]) == row[key + '_sha256']
        before[str(DATA / row[key])] = row[key + '_sha256']
    phases.append(dict(row, executed_again=False, parent_receipt_sha256=sha(PARENT)))
    text = (DATA / row['ir']).read_text(encoding='utf-8')
    blocks[row['name']] = [dict(name=m.group(1), text=m.group(0)) for m in
        re.finditer(r'^function #\d+ ([^\n]+)\n.*?(?=^function #|\Z)', text, re.M | re.S)]
def block(label, name):
    matches = [b['text'] for b in blocks[label] if b['name'] == name]
    assert len(matches) == 1
    return matches[0]
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = 'C:/Python/Python314/Lib'
command = [str(RELEASE / 'xlang3.exe'), str(probe)]
stdout, stderr = DIR / 'stdout.log', DIR / 'stderr.log'
with stdout.open('wb') as out_stream, stderr.open('wb') as err_stream:
    child = subprocess.run(command, cwd=ROOT, env=env, stdout=out_stream, stderr=err_stream, timeout=60)
phases.append(dict(name='unpickle-inputs-r2', command=command, exit_code=child.returncode,
                   stdout=str(stdout.relative_to(DATA)), stdout_sha256=sha(stdout),
                   stderr=str(stderr.relative_to(DATA)), stderr_sha256=sha(stderr), executed_again=True))
date_source = Path('C:/Python/Python314/Lib/_pydatetime.py')
date_class = next(n for n in ast.parse(date_source.read_bytes()).body if isinstance(n, ast.ClassDef) and n.name == 'date')
date_new_line = next(n.lineno for n in date_class.body if isinstance(n, ast.FunctionDef) and n.name == '__new__')
date_blocks = [b['text'] for b in blocks['date-original'] if b['name'] == '__new__' and
               re.search(r'^  first_line: ' + str(date_new_line) + r'$', b['text'], re.M)]
finalizer = block('finalizer', 'construct_results')
checks = dict(
    ordinary_call=' Call ' in block('main', 'direct_call'),
    exact_tuple_call_ex='CallEx' in block('main', 'ordinary_call'),
    finalizer_same_call_destination=all(s in finalizer for s in (
        '7: Call ', 'dst=8 a=6', '8: Pop ', 'a=8', '9: Jump ', 'dst=5')),
    original_reduce_call_ex='CallEx' in block('pickle-original', 'load_reduce'),
    python_date_new_body=len(date_blocks) == 1 and all(s in date_blocks[0] for s in (
        'generator: false', 'async: false', 'coroutine: false')),
    original_body_observable_inputs=child.returncode == 0 and stderr.read_bytes() == b'' and
        len([l for l in stdout.read_text().splitlines() if l.startswith('PASS ')]) == 3,
)
stable = all(sha(Path(n)) == h for n, h in before.items()) and tree(RELEASE) == build['binaries_sha256']
passed = all(checks.values()) and stable
record = dict(status='eligibility_inputs_passed' if passed else 'eligibility_inputs_failed',
              terminal=True, passed=passed, checks=checks, phases=phases,
              parent_receipt_sha256=sha(PARENT), date_new_first_line=date_new_line,
              hashes_before=before, hashes_after={n: sha(Path(n)) for n in before}, hashes_unchanged=stable,
              source_sha256=parent['source_sha256'], binaries_sha256=build['binaries_sha256'],
              controller_sha256=sha(__file__), timed=False,
              correction='Original _pydatetime intentionally sets __name__=datetime; use its code filename for Python body origin. IR function names may repeat; select date new by original AST line.',
              scope=parent['scope'], runtime_guard_limits=parent['runtime_guard_limits'])
OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print(record['status'], checks, 'receipt', sha(OUT), flush=True)
if not passed:
    print(stderr.read_text(errors='replace')[-1500:])
raise SystemExit(0 if passed else 1)
