"""Capture original IR and observable admission inputs without hot counters."""
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
BUILD = DATA / 'python-new-vm-continuation-r4-build-20261009.json'
APP = DATA / 'python-new-vm-continuation-r4-applied-source-20261009.json'
PREFIX = 'python-new-vm-continuation-r4-eligibility-20261009'
OUT = DATA / (PREFIX + '.json')
DIR = DATA / PREFIX
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated and not sys.flags.optimize
assert sha(BUILD) == 'd4dc98f0c23e1d2b902a6a658cc1d7211c59ca01caa8f10a4a95ffb5d67644b9'
assert sha(APP) == '2c89133fbe3c3a4c0d13a1652b62362d69be1c79b07e197186682eac664cb0a8'
build, app = json.loads(BUILD.read_bytes()), json.loads(APP.read_bytes())
assert build['passed'] and tree(RELEASE) == build['binaries_sha256']
assert not DIR.exists() and not OUT.exists()
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = 'C:/Python/Python314/Lib'
jobs = (
    ('main', ROOT / 'tests/fixtures/core/python_new_vm_continuation.py', []),
    ('finalizer', ROOT / 'tests/fixtures/core/python_new_completion_finalizer.py', []),
    ('pickle-original', Path('C:/Python/Python314/Lib/pickle.py'), ['--help']),
    ('date-original', Path('C:/Python/Python314/Lib/_pydatetime.py'), []),
    ('unpickle-inputs', ROOT / 'scratch/performance/python-new-original-unpickle-probe-root-20261009.py', []),
)
before = {str(p): sha(p) for p in (APP, BUILD, Path(__file__))}
before.update({str(p): sha(p) for _, p, _ in jobs})
before.update({str(ROOT / n): h for n, h in app['source_sha256'].items()})
phases, blocks = [], {}
for label, source, extra in jobs:
    folder = DIR / label
    folder.mkdir(parents=True)
    command = [str(RELEASE / 'xlang3.exe'), '--dump-ir', '--debug-dir', str(folder), str(source), *extra]
    stdout, stderr = folder / 'stdout.log', folder / 'stderr.log'
    with stdout.open('wb') as out_stream, stderr.open('wb') as err_stream:
        child = subprocess.run(command, cwd=ROOT, env=env, stdout=out_stream, stderr=err_stream, timeout=60)
    ir = folder / (source.stem + '.ir.txt')
    phases.append(dict(name=label, command=command, exit_code=child.returncode,
                       stdout=str(stdout.relative_to(DATA)), stdout_sha256=sha(stdout),
                       stderr=str(stderr.relative_to(DATA)), stderr_sha256=sha(stderr),
                       ir=str(ir.relative_to(DATA)) if ir.exists() else None,
                       ir_sha256=sha(ir) if ir.exists() else None))
    print('IR/probe', label, 'exit', child.returncode, flush=True)
    if child.returncode != 0 or not ir.exists():
        print(stderr.read_text(errors='replace')[-1500:], flush=True)
        break
    text = ir.read_text(encoding='utf-8')
    blocks[label] = {m.group(1): m.group(0) for m in re.finditer(r'^function #\d+ ([^\n]+)\n.*?(?=^function #|\Z)', text, re.M | re.S)}
def block(label, suffix):
    matches = [b for name, b in blocks.get(label, {}).items() if name == suffix or name.endswith('.' + suffix)]
    assert len(matches) == 1, (label, suffix, list(blocks.get(label, {}))[:12])
    return matches[0]
checks = {}
if len(phases) == 5 and all(p['exit_code'] == 0 and p['ir'] for p in phases):
    checks['ordinary_call'] = ' Call ' in block('main', 'direct_call')
    checks['exact_tuple_call_ex'] = 'CallEx' in block('main', 'ordinary_call')
    finalizer = block('finalizer', 'construct_results')
    checks['same_call_site_repeated_by_loop'] = ' Call ' in finalizer and ('Jump ' in finalizer or 'ForIter' in finalizer)
    checks['original_reduce_call_ex'] = 'CallEx' in block('pickle-original', 'load_reduce')
    checks['python_date_new_body'] = bool(block('date-original', 'date.__new__'))
    probe_stdout = (DIR / 'unpickle-inputs/stdout.log').read_text(encoding='utf-8')
    checks['original_body_and_observable_inputs'] = len([l for l in probe_stdout.splitlines() if l.startswith('PASS ')]) == 3
stable = all(sha(Path(n)) == h for n, h in before.items()) and tree(RELEASE) == build['binaries_sha256']
passed = len(checks) == 6 and all(checks.values()) and stable
record = dict(status='eligibility_inputs_passed' if passed else 'eligibility_inputs_failed', terminal=True,
              passed=passed, checks=checks, phases=phases, hashes_before=before,
              hashes_after={n: sha(Path(n)) for n in before}, hashes_unchanged=stable,
              source_sha256=app['source_sha256'], binaries_sha256=build['binaries_sha256'],
              controller_sha256=sha(__file__), timed=False,
              scope='Original emitted IR plus observed guard inputs and unchanged workload execution; no dynamic admission counter, occurrence frequency, CPU fraction or speed result.',
              runtime_guard_limits='Internal pending-event/debug state not directly observed; no claim every dynamic occurrence took the shortcut.')
OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print(record['status'], checks, 'receipt', sha(OUT), flush=True)
raise SystemExit(0 if passed else 1)
