"""Establish exact CP3.14.7 and accepted-XLang semantic outputs before editing the engine."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path('D:/CantorAI/xlang3')
data = root / 'doc/performance/data'
fixture = root / 'scratch/performance/two-argument-double-ir-plan-fixture-r2-proposed-20261009.py'
expected = fixture.with_suffix('.out')
cp = Path('C:/Python/Python314/python.exe')
release = root / 'build-repro/main-verify-20261006/Release'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == cp.resolve()
assert sha(fixture) == '201cd7c2ada28a95627c21cb7840b162dae83affbb88207b26321ff9d6c97811'
assert sha(expected) == 'eb946542485ff0adfd3c5595a5a3e184c2b91a7bc894abfe01ea1d5e189f49ef'
c = json.loads((root / 'build-repro/controls/gc-generic-cycles-r7b-accepted-20261009/provenance.json').read_bytes())
assert all(sha(release / p) == h for p, h in c['release_sha256'].items())
pins = {str(fixture): sha(fixture), str(expected): sha(expected), str(cp): sha(cp), str(cp.with_name('python314.dll')): sha(cp.with_name('python314.dll'))}
for p, h in c['unowned_tracked_dirty_sha256'].items():
    pins[str(root / p)] = h
assert all(sha(p) == h for p, h in pins.items())
prefix = 'two-argument-double-ir-plan-r2-reference-20261010'
assert not any(data.glob(prefix + '*'))
env = os.environ.copy()
for k in tuple(env):
    if k.startswith('PYTHON') or k.startswith('XLANG3_'):
        env.pop(k, None)
env['XLANG3_PYTHON_LIB'] = str(cp.parent / 'Lib')
env['PYTHONIOENCODING'] = 'utf-8'
normalize = lambda b: b.replace(b'\r\r\n', b'\n').replace(b'\r\n', b'\n')
rows = []
for name, exe, flags in [('cpython3147', cp, ['-I']), ('xlang3-accepted', release / 'xlang3.exe', [])]:
    stdout, stderr = [data / (prefix + '-' + name + suffix) for suffix in ('.stdout.log', '.stderr.log')]
    command = [str(exe), *flags, str(fixture)]
    with stdout.open('xb') as out, stderr.open('xb') as err:
        result = subprocess.run(command, cwd=root, env=env, stdin=subprocess.DEVNULL,
            stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW, timeout=60)
    row = dict(name=name, command=command, exit_code=result.returncode,
        stdout_sha256=sha(stdout), stderr_sha256=sha(stderr),
        passed=result.returncode == 0 and stderr.read_bytes() == b'' and normalize(stdout.read_bytes()) == normalize(expected.read_bytes()))
    rows.append(row)
    print(name, 'PASS' if row['passed'] else 'FAIL', flush=True)
unchanged = all(sha(p) == h for p, h in pins.items()) and all(sha(release / p) == h for p, h in c['release_sha256'].items())
record = dict(terminal=True, scored=False, status='semantic_reference_passed' if unchanged and all(r['passed'] for r in rows) else 'semantic_reference_failed',
    phases=rows, inputs_unchanged=unchanged, pins=pins, controller_sha256=sha(__file__),
    candidate_engine_applied=False, scope='Seven proposed semantic groups on both fixed references before engine changes; no optimization eligibility or speed claim.')
out = data / (prefix + '.json')
out.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
print(record['status'], sha(out), flush=True)
raise SystemExit(0 if record['status'] == 'semantic_reference_passed' else 1)
