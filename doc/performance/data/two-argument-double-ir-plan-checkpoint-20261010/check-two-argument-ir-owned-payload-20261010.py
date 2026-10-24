"""One finite CP/control/candidate lifetime probe, without timing claims."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path('D:/CantorAI/xlang3')
data = root / 'doc/performance/data'
cp = Path('C:/Python/Python314/python.exe')
release = root / 'build-repro/main-verify-20261006/Release'
control = root / 'build-repro/controls/gc-generic-cycles-r7b-accepted-20261009'
fixture = root / 'scratch/performance/two-argument-ir-owned-payload-probe-proposed-20261010.py'
expected = fixture.with_suffix('.out')
prefix = 'two-argument-ir-owned-payload-20261010'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == cp.resolve()
assert sha(fixture) == 'e0af5f85a705df2da806303d1f3a66cf4f53d8cafc49928b438550cf6484b6ff'
assert sha(expected) == '51b9c19761d329c79fda08a5d84d7acdccaeed44311b00c94345767986e1c4f4'
app_path = data / 'two-argument-double-ir-plan-r2-trial-20261010-application.json'
build_path = data / 'two-argument-double-ir-plan-r2-trial-20261010-build.json'
assert sha(app_path) == 'a68a609265662e89d8657a13bf00fde0ca174e4661122a7ab0e810705a4ecaac'
assert sha(build_path) == 'ba14f17d762bf33ad703cf31914b2feb06d0bc938d8774a7c8a858cf4aa79f9e'
app = json.loads(app_path.read_bytes())
build = json.loads(build_path.read_bytes())
accepted = json.loads((control / 'provenance.json').read_bytes())
pins = {str(root / p): h for p, h in app['source_sha256'].items()}
for key in ('unowned_tracked_dirty_sha256', 'protected_test_input_sha256'):
    pins.update({str(root / p): h for p, h in app[key].items()})
pins.update({str(release / p): h for p, h in build['release_sha256'].items()})
pins.update({str(root / 'build-repro/Release' / p): h for p, h in app['fixed_baseline_sha256'].items()})
pins.update({str(control / 'Release' / p): h for p, h in accepted['release_sha256'].items()})
pins.update({str(p): sha(p) for p in (fixture, expected, cp, cp.with_name('python314.dll'))})
assert all(sha(p) == h for p, h in pins.items())
assert not any(data.glob(prefix + '*'))
env = os.environ.copy()
for key in tuple(env):
    if key.startswith(('PYTHON', 'XLANG3_')):
        env.pop(key, None)
env['XLANG3_PYTHON_LIB'] = str(cp.parent / 'Lib')
env['PYTHONIOENCODING'] = 'utf-8'
normal = lambda b: b.replace(b'\r\r\n', b'\n').replace(b'\r\n', b'\n')
rows = []
for name, exe, flags in (
    ('cpython3147', cp, ['-I']),
    ('xlang3-accepted', control / 'Release/xlang3.exe', []),
    ('xlang3-candidate', release / 'xlang3.exe', []),
):
    stdout = data / (prefix + '-' + name + '.stdout.log')
    stderr = data / (prefix + '-' + name + '.stderr.log')
    command = [str(exe), *flags, str(fixture)]
    with stdout.open('xb') as out, stderr.open('xb') as err:
        result = subprocess.run(command, cwd=root, env=env, stdin=subprocess.DEVNULL,
            stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW, timeout=30)
    passed = result.returncode == 0 and stderr.read_bytes() == b'' and normal(stdout.read_bytes()) == normal(expected.read_bytes())
    rows.append(dict(name=name, command=command, exit_code=result.returncode, passed=passed,
        stdout=stdout.name, stderr=stderr.name, stdout_sha256=sha(stdout), stderr_sha256=sha(stderr)))
    print(name, 'PASS' if passed else 'FAIL', flush=True)
unchanged = all(sha(p) == h for p, h in pins.items())
passed = unchanged and all(row['passed'] for row in rows)
record = dict(terminal=True, passed=passed, scored=False, phases=rows, inputs_unchanged=unchanged,
    controller_sha256=sha(__file__), pins=pins,
    scope='Exact CP-first finalizer observations; no speed or shortcut admission claim.')
path = data / (prefix + '.json')
path.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
print('Lifetime probe', 'PASS' if passed else 'FAIL', sha(path), flush=True)
raise SystemExit(0 if passed else 1)
