"""Candidate-only lifetime recheck, retaining strict CP baseline failures."""
import argparse
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
CP = Path('C:/Python/Python314/python.exe')
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for label in ('application', 'build'):
        parser.add_argument('--' + label, type=Path, required=True)
        parser.add_argument('--' + label + '-sha256', required=True)
    args = parser.parse_args()
    assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CP.resolve()
    assert sys.flags.isolated and not sys.flags.optimize
    prefix = 'two-argument-ir-guarded-lifetime-r2-20261010'
    assert not any(DATA.glob(prefix + '*'))
    for label in ('application', 'build'):
        assert sha(getattr(args, label)) == getattr(args, label + '_sha256')
    app, built = (json.loads(getattr(args, label).read_bytes()) for label in ('application', 'build'))
    assert app['passed'] and built['passed'] and built['terminal']
    assert built['application_sha256'] == args.application_sha256 and built['source_sha256'] == app['source_sha256']
    pins = {str(ROOT / p): h for p, h in app['source_sha256'].items()}
    pins.update({str(RELEASE / p): h for p, h in built['release_sha256'].items()})
    for key in ('protected_test_input_sha256', 'unowned_tracked_dirty_sha256'):
        pins.update({str(ROOT / p): h for p, h in app[key].items()})
    pins.update({str(ROOT / 'build-repro/Release' / p): h for p, h in app['fixed_baseline_sha256'].items()})
    references = (
        ('temporary', 'two-argument-ir-lifetime-20261010.json', 'e180d4069d2521e5d994610549c5c357b9090cfa5706da106f206917e974d14e'),
        ('alias', 'two-argument-ir-lifetime-alias-20261010.json', 'bfd5aecaff018b543a434feb068a7762e87f69b66735dbaa6a76491e2bb278fb'),
        ('replacement', 'two-argument-ir-lifetime-replacement-20261010.json', '56338dcc5f8b1a77dc84341ba458c4805b4395912a401268d49f60c3c19633bf'),
    )
    env = os.environ.copy()
    for key in tuple(env):
        if key.startswith(('PYTHON', 'XLANG3_')):
            env.pop(key, None)
    env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
    env['PYTHONIOENCODING'] = 'utf-8'
    normalize = lambda b: b.replace(b'\r\r\n', b'\n').replace(b'\r\n', b'\n')
    # Object addresses differ across processes; retain raw logs and only mask
    # hexadecimal addresses for the control-relative comparison, never names.
    address_normal = lambda b: re.sub(rb'0x[0-9a-fA-F]+', b'0xADDRESS', normalize(b))
    rows = []
    for name, reference_name, reference_sha in references:
        reference_path = DATA / reference_name
        assert sha(reference_path) == reference_sha
        pins[str(reference_path)] = reference_sha
        reference = json.loads(reference_path.read_bytes())
        accepted = next(p for p in reference['phases'] if p['name'] == 'xlang3-accepted')
        cp = next(p for p in reference['phases'] if p['name'] == 'cpython3147')
        assert cp['passed'] and cp['exit_code'] == 0 and reference['inputs_unchanged']
        for path, h in reference['pins'].items():
            if '/scratch/performance/' in path.replace('\\', '/') or Path(path).resolve() in (CP.resolve(), CP.with_name('python314.dll').resolve()):
                pins[path] = h
        stdout = DATA / (prefix + '-' + name + '.stdout.log')
        stderr = DATA / (prefix + '-' + name + '.stderr.log')
        command = [str(RELEASE / 'xlang3.exe'), *accepted['command'][1:]]
        raw_reference = {}
        for stream in ('stdout', 'stderr'):
            path = DATA / accepted[stream]
            assert sha(path) == accepted[stream + '_sha256']
            pins[str(path)] = sha(path)
            raw_reference[stream] = path.read_bytes()
        assert all(sha(p) == h for p, h in pins.items())
        with stdout.open('xb') as out, stderr.open('xb') as err:
            result = subprocess.run(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW, timeout=30)
        same = result.returncode == accepted['exit_code'] and all(
            address_normal(path.read_bytes()) == address_normal(raw_reference[stream])
            for stream, path in (('stdout', stdout), ('stderr', stderr)))
        row = dict(name=name, command=command, exit_code=result.returncode,
            preserved_accepted_observation=same, strict_cp_parity_passed=accepted['passed'] and same,
            reference_receipt_sha256=reference_sha, stdout=stdout.name, stderr=stderr.name,
            stdout_sha256=sha(stdout), stderr_sha256=sha(stderr))
        rows.append(row)
        print(name, 'CONTROL PRESERVED' if same else 'CONTROL DIFFERS', flush=True)
    unchanged = all(sha(p) == h for p, h in pins.items())
    preserved = unchanged and all(row['preserved_accepted_observation'] for row in rows)
    record = dict(terminal=True, scored=False, control_relative_passed=preserved,
        strict_cp_parity_passed=all(row['strict_cp_parity_passed'] for row in rows),
        phases=rows, inputs_unchanged=unchanged, pins=pins,
        application_sha256=args.application_sha256, build_sha256=args.build_sha256,
        controller_path=str(Path(__file__).resolve()), controller_sha256=sha(__file__),
        scope='Control-relative frame/lifetime observations; existing strict CP failures remain failures.')
    path = DATA / (prefix + '.json')
    path.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
    print('Lifetime control-relative', preserved, 'strict CP parity', record['strict_cp_parity_passed'], sha(path), flush=True)
    return 0 if preserved else 1

if __name__ == '__main__':
    raise SystemExit(main())
