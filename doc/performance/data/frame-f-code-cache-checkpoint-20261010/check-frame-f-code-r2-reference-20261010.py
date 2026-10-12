"""Unscored CP-first reference and unchanged XLang3 frame-code behavior."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
CONTROL = ROOT / 'build-repro/controls/gc-generic-cycles-r7b-accepted-20261009/Release'
CURRENT = ROOT / 'build-repro/main-verify-20261006/Release'
PREFIX = 'frame-f-code-r2-reference-20261010'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
normal = lambda b: b.replace(b'\r\r\n', b'\n').replace(b'\r\n', b'\n')

def main():
    assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated and not sys.flags.optimize
    assert Path(sys.executable).resolve() == CP.resolve()
    assert not any(DATA.glob(PREFIX + '*')), 'Never overwrite reference evidence'
    fixture = ROOT / 'scratch/performance/frame-f-code-lazy-cache-r2-proposed-20261010-candidates/tests/fixtures/core/frame_f_code_cache.py'
    expected = ROOT / 'scratch/performance/frame-f-code-lazy-cache-r2-proposed-20261010-candidates/tests/fixtures/expected/frame_f_code_cache.out'
    probe = ROOT / 'scratch/performance/frame-f-code-preexisting-identity-metadata-probe-proposed-20261010.py'
    pins = {str(fixture): 'c59eb4b0869352fe2e2d9a140215d23a43432f997176d4a19cd08a97f104be17',
            str(expected): '2e3f8ca90f1a219f82b7405ce1931a145534440a81fb09f331d4d01d6c2e0917',
            str(CONTROL / 'xlang3.exe'): '02bcd889a0872266cd19ad9988c5027e953f5dca796db8dd6a9c418d88c28607',
            str(CONTROL / 'xlang3_runtime.dll'): '5dfa38e5a3730e2d31665ef66f662fae3835f14c27434167e766dbdcd6f370f8'}
    # These references are semantic facts, not a performance certification.
    # Preserve the strict assertions in both probes, including existing failures.
    for path in (CP, CP.parent / 'python314.dll', probe, Path(__file__).resolve()):
        pins[str(path)] = sha(path)
    app_path = DATA / 'two-argument-double-ir-plan-r3-trial-20261010-application.json'
    build_path = DATA / 'two-argument-double-ir-plan-r3-trial-20261010-build.json'
    pins[str(app_path)] = 'b6aec3660100c754eff3dea59a67ddd42f92842f57138cfe17fde7594181e02a'
    pins[str(build_path)] = '0e24b3ff4053f049c1febb43e49a22b3a9d8c99447839dac0e58af1b184eba0f'
    assert all(sha(p) == h for p, h in pins.items())
    app, build = [json.loads(p.read_bytes()) for p in (app_path, build_path)]
    assert app['passed'] and build['passed'] and build['source_sha256'] == app['source_sha256']
    pins.update({str(ROOT / p): h for p, h in app['source_sha256'].items()})
    pins.update({str(CURRENT / p): h for p, h in build['release_sha256'].items()})
    assert all(sha(p) == h for p, h in pins.items())
    env = {k: v for k, v in os.environ.items() if not k.startswith(('PYTHON', 'XLANG3_'))}
    env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
    env['PYTHONIOENCODING'] = 'utf-8'
    cases = [('fixture', fixture, [], expected.read_bytes()),
             ('canonical', probe, ['canonical'], b'frame uses exact canonical function code object\n'),
             ('assigned_metadata', probe, ['assigned_metadata'], b'frame preserves exact assigned code object and metadata\n')]
    results = []
    for label, executable, flags in [('cpython3147', CP, ['-I']),
                                     ('accepted_xlang3', CONTROL / 'xlang3.exe', []),
                                     ('guarded_numeric_xlang3', CURRENT / 'xlang3.exe', [])]:
        for case, source, arguments, wanted in cases:
            command = [str(executable), *flags, str(source), *arguments]
            out, err = [DATA / (PREFIX + '-' + label + '-' + case + '.' + s + '.log') for s in ('stdout', 'stderr')]
            with out.open('xb') as stdout, err.open('xb') as stderr:
                run = subprocess.run(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                                     stdout=stdout, stderr=stderr, timeout=60,
                                     creationflags=subprocess.CREATE_NO_WINDOW)
            matched = run.returncode == 0 and not err.read_bytes() and normal(out.read_bytes()) == normal(wanted)
            results.append(dict(runtime=label, case=case, command=command, exit_code=run.returncode,
                                strict_expected_match=matched, direct_child_waited=True,
                                stdout=out.name, stdout_sha256=sha(out), stderr=err.name, stderr_sha256=sha(err)))
            print(label, case, 'PASS' if matched else 'STRICT_FAILURE', flush=True)
    unchanged = all(sha(p) == h for p, h in pins.items())
    cp_passed = all(r['strict_expected_match'] for r in results if r['runtime'] == 'cpython3147')
    path = DATA / (PREFIX + '.json')
    path.write_text(json.dumps(dict(terminal=True, scored=False, reference_passed=cp_passed and unchanged,
        inputs_unchanged=unchanged, controller_sha256=sha(__file__), pins=pins, results=results,
        scope='CP-first strict reference and pre-change X behavior. No cache applied, benchmark run, runtime parity or acceptance claim.'), indent=2) + '\n', encoding='utf-8', newline='\n')
    print('Reference receipt', sha(path), flush=True)
    return 0 if cp_passed and unchanged else 1

if __name__ == '__main__':
    raise SystemExit(main())
