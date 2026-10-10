"""Eight semantic groups on the built candidate; no speed/eligibility claim."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path('D:/CantorAI/xlang3')
data = root / 'doc/performance/data'
release = root / 'build-repro/main-verify-20261006/Release'
fixture = root / 'tests/fixtures/core/two_argument_double_ir_plan.py'
expected = root / 'tests/fixtures/expected/two_argument_double_ir_plan.out'
prefix = 'two-argument-double-ir-plan-r2-candidate-semantic-20261010'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
app_path = data / 'two-argument-double-ir-plan-r2-trial-20261010-application.json'
build_path = data / 'two-argument-double-ir-plan-r2-trial-20261010-build.json'
assert sha(app_path) == 'a68a609265662e89d8657a13bf00fde0ca174e4661122a7ab0e810705a4ecaac'
assert sha(build_path) == 'ba14f17d762bf33ad703cf31914b2feb06d0bc938d8774a7c8a858cf4aa79f9e'
app = json.loads(app_path.read_bytes())
build = json.loads(build_path.read_bytes())
assert app['passed'] and build['passed'] and build['terminal']
assert not any(data.glob(prefix + '*'))
pins = {str(root / p): h for p, h in app['source_sha256'].items()}
pins.update({str(release / p): h for p, h in build['release_sha256'].items()})
pins.update({str(root / p): h for p, h in app['unowned_tracked_dirty_sha256'].items()})
pins.update({str(root / p): h for p, h in app['protected_test_input_sha256'].items()})
pins.update({str(root / 'build-repro/Release' / p): h for p, h in app['fixed_baseline_sha256'].items()})
assert all(sha(p) == h for p, h in pins.items())
env = os.environ.copy()
for key in tuple(env):
    if key.startswith(('PYTHON', 'XLANG3_')):
        env.pop(key, None)
env['XLANG3_PYTHON_LIB'] = 'C:/Python/Python314/Lib'
env['PYTHONIOENCODING'] = 'utf-8'
command = [str(release / 'xlang3.exe'), str(fixture)]
stdout = data / (prefix + '.stdout.log')
stderr = data / (prefix + '.stderr.log')
with stdout.open('xb') as out, stderr.open('xb') as err:
    result = subprocess.run(command, cwd=root, env=env, stdin=subprocess.DEVNULL, stdout=out,
                            stderr=err, creationflags=subprocess.CREATE_NO_WINDOW, timeout=60)
normal = lambda b: b.replace(b'\r\r\n', b'\n').replace(b'\r\n', b'\n')
unchanged = all(sha(p) == h for p, h in pins.items())
passed = result.returncode == 0 and stderr.read_bytes() == b'' and normal(stdout.read_bytes()) == normal(expected.read_bytes()) and unchanged
receipt = dict(terminal=True, passed=passed, scored=False, command=command, exit_code=result.returncode,
               application_sha256=sha(app_path), build_sha256=sha(build_path), controller_sha256=sha(__file__),
               inputs_unchanged=unchanged, stdout=stdout.name, stdout_sha256=sha(stdout),
               stderr=stderr.name, stderr_sha256=sha(stderr), pins=pins,
               scope='Eight candidate semantic groups; no observed plan-hit or performance claim')
path = data / (prefix + '.json')
path.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8', newline='\n')
print('Candidate semantic', 'PASS' if passed else 'FAIL', sha(path), flush=True)
raise SystemExit(0 if passed else 1)
