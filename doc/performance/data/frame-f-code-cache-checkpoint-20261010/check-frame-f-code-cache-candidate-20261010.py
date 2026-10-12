"""Fresh strict frame and numeric fixtures on the combined Release candidate."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
PREFIX = 'frame-f-code-cache-r2-candidate-semantic-20261010'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
normal = lambda b: b.replace(b'\r\r\n', b'\n').replace(b'\r\n', b'\n')

def main():
    assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated and not sys.flags.optimize
    assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
    assert not any(DATA.glob(PREFIX + '*'))
    app_path = DATA / 'frame-f-code-cache-r2-trial-20261010-application.json'
    build_path = DATA / 'frame-f-code-cache-r2-trial-20261010-build.json'
    assert sha(app_path) == '37f0cc99feec21b72481a257b287e92fb04ceb047f309aaaa6fe801338390f3a'
    assert sha(build_path) == '249f35338294c2e6a8e822ed107db3035e2c50b47b5dd46acdbfed26c607a081'
    app, build = [json.loads(p.read_bytes()) for p in (app_path, build_path)]
    assert app['passed'] and build['passed'] and build['owned_child_cleanup_completed']
    assert build['source_sha256'] == app['source_sha256'] and len(app['source_sha256']) == 148
    pins = {str(ROOT / p): h for p, h in app['source_sha256'].items()}
    pins.update({str(RELEASE / p): h for p, h in build['release_sha256'].items()})
    for key in ('unowned_tracked_dirty_sha256', 'protected_test_input_sha256'):
        pins.update({str(ROOT / p): h for p, h in app[key].items()})
    pins.update({str(ROOT / 'build-repro/Release' / p): h for p, h in app['fixed_baseline_sha256'].items()})
    assert all(sha(p) == h for p, h in pins.items())
    env = {k: v for k, v in os.environ.items() if not k.startswith(('PYTHON', 'XLANG3_'))}
    env['XLANG3_PYTHON_LIB'] = 'C:/Python/Python314/Lib'
    env['PYTHONIOENCODING'] = 'utf-8'
    results = []
    for case in ('frame_f_code_cache', 'two_argument_double_ir_plan'):
        source = ROOT / 'tests/fixtures/core' / (case + '.py')
        expected = ROOT / 'tests/fixtures/expected' / (case + '.out')
        out, err = [DATA / (PREFIX + '-' + case + '.' + stream + '.log') for stream in ('stdout', 'stderr')]
        command = [str(RELEASE / 'xlang3.exe'), str(source)]
        with out.open('xb') as stdout, err.open('xb') as stderr:
            child = subprocess.run(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                                   stdout=stdout, stderr=stderr, timeout=60,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        passed = child.returncode == 0 and not err.read_bytes() and normal(out.read_bytes()) == normal(expected.read_bytes())
        results.append(dict(case=case, passed=passed, exit_code=child.returncode, command=command,
                            stdout=out.name, stdout_sha256=sha(out), stderr=err.name, stderr_sha256=sha(err),
                            expected_sha256=sha(expected), direct_child_waited=True))
        print(case, 'PASS' if passed else 'FAIL', flush=True)
    unchanged = all(sha(p) == h for p, h in pins.items())
    passed = unchanged and all(r['passed'] for r in results)
    path = DATA / (PREFIX + '.json')
    path.write_text(json.dumps(dict(terminal=True, passed=passed, scored=False,
        application_sha256=sha(app_path), build_sha256=sha(build_path), controller_sha256=sha(__file__),
        results=results, pins=pins, inputs_unchanged=unchanged,
        scope='Fresh two strict registered fixtures only; no full-suite, canonical-code parity or performance acceptance.'), indent=2) + '\n', encoding='utf-8', newline='\n')
    print('Focused candidate receipt', sha(path), flush=True)
    return 0 if passed else 1

if __name__ == '__main__':
    raise SystemExit(main())
