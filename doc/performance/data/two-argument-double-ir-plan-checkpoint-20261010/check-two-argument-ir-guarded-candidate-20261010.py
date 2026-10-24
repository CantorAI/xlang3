"""Fresh unchanged eight-group semantics on the guarded candidate."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
CP = Path('C:/Python/Python314/python.exe')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
FIXTURE = ROOT / 'tests/fixtures/core/two_argument_double_ir_plan.py'
EXPECTED = ROOT / 'tests/fixtures/expected/two_argument_double_ir_plan.out'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for label in ('application', 'build'):
        parser.add_argument('--' + label, type=Path, required=True)
        parser.add_argument('--' + label + '-sha256', required=True)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated and not sys.flags.optimize
    assert Path(sys.executable).resolve() == CP.resolve()
    assert re.fullmatch(r'two-argument-double-ir-plan-r[3-9][0-9]*-candidate-semantic-20261010', args.prefix)
    assert not any(DATA.glob(args.prefix + '*'))
    assert sha(FIXTURE) == '406434854572605d6e699d54bcd2c1b9b524b1f403aeec70f7190c5f57b514f4'
    assert sha(EXPECTED) == '154e54becd1e12cb2d4a8461bc8739f786a9a9136a1cbec8be8bf40ad6adaeac'
    for label in ('application', 'build'):
        assert sha(getattr(args, label)) == getattr(args, label + '_sha256')
    app = json.loads(args.application.read_bytes())
    build = json.loads(args.build.read_bytes())
    assert app['terminal'] and app['passed'] and app['status'] == 'applied'
    assert build['terminal'] and build['passed'] and build['exit_code'] == 0 and build['status'] == 'build_passed'
    assert build['application_sha256'] == args.application_sha256 and build['source_sha256'] == app['source_sha256']
    pins = {str(ROOT / p): h for p, h in app['source_sha256'].items()}
    pins.update({str(RELEASE / p): h for p, h in build['release_sha256'].items()})
    for key in ('unowned_tracked_dirty_sha256', 'protected_test_input_sha256'):
        pins.update({str(ROOT / p): h for p, h in app[key].items()})
    pins.update({str(ROOT / 'build-repro/Release' / p): h for p, h in app['fixed_baseline_sha256'].items()})
    assert all(sha(p) == h for p, h in pins.items())
    env = os.environ.copy()
    for key in tuple(env):
        if key.startswith(('PYTHON', 'XLANG3_')):
            env.pop(key, None)
    env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
    env['PYTHONIOENCODING'] = 'utf-8'
    stdout, stderr = [DATA / (args.prefix + '.' + stream + '.log') for stream in ('stdout', 'stderr')]
    command = [str(RELEASE / 'xlang3.exe'), str(FIXTURE)]
    with stdout.open('xb') as out, stderr.open('xb') as err:
        result = subprocess.run(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL, stdout=out, stderr=err,
            creationflags=subprocess.CREATE_NO_WINDOW, timeout=60)
    normal = lambda b: b.replace(b'\r\r\n', b'\n').replace(b'\r\n', b'\n')
    unchanged = all(sha(p) == h for p, h in pins.items())
    passed = result.returncode == 0 and stderr.read_bytes() == b'' and normal(stdout.read_bytes()) == normal(EXPECTED.read_bytes()) and unchanged
    record = dict(terminal=True, passed=passed, scored=False, command=command, exit_code=result.returncode,
        application_sha256=args.application_sha256, build_sha256=args.build_sha256,
        controller_path=str(Path(__file__).resolve()), controller_sha256=sha(__file__),
        inputs_unchanged=unchanged, stdout=stdout.name, stdout_sha256=sha(stdout),
        stderr=stderr.name, stderr_sha256=sha(stderr), pins=pins,
        scope='Unchanged eight semantic groups; no shortcut admission or performance claim.')
    path = DATA / (args.prefix + '.json')
    path.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
    print('Guarded semantic', 'PASS' if passed else 'FAIL', sha(path), flush=True)
    return 0 if passed else 1

if __name__ == '__main__':
    raise SystemExit(main())
