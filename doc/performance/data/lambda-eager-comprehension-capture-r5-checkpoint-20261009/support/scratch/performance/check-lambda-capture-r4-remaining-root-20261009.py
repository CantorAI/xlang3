"""Run each not-yet-executed focus check once on the unchanged failed candidate."""
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
BUILD = DATA / 'lambda-eager-comprehension-capture-r4-build-terminal-20261009.json'
OUT = DATA / 'lambda-eager-comprehension-capture-r4-focused-remaining-20261009.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7)
assert sys.flags.isolated and not sys.flags.optimize and not OUT.exists()
assert sha(BUILD) == 'a14343ff797015d3444f40da210e927a6a1991b218d877956825754f1049ceb0'
build = json.loads(BUILD.read_bytes())
sources = build['source_sha256']
names = ['comprehension_target_lifetime', 'comprehension_iterable_scope', 'nested_comprehension_capture',
         'nested_generator_comprehension_capture', 'compile_ast_list_comprehension',
         'compile_ast_set_dict_comprehension', 'inline_property_dynamic_attr',
         'ordinary_canonical_slot_constructor', 'function_code_replacement', 'class_method_annotation_capture', 'closure_cell_semantics', 'nonlocal_counter',
         'private_slots_mangling', 'private_generator_name_mangling', 'annotation_lambda_ast',
         'nested_comprehensions', 'comprehension_multiple_filters', 'compile_ast_lambda_generator',
         'generator_expressions']
before = {str(ROOT / p): h for p, h in sources.items()}
before.update({str(RELEASE / p): h for p, h in build['binaries_sha256'].items()})
for p in [Path(__file__), BUILD, CP]:
    before[str(p)] = sha(p)
for name in names:
    for p in [ROOT / ('tests/fixtures/core/' + name + '.py'), ROOT / ('tests/fixtures/expected/' + name + '.out')]:
        before[str(p)] = sha(p)
assert all(sha(p) == h for p, h in before.items())
record = dict(status='running', terminal=False, timed=False, hashes_before=before, phases=[],
              scope='Nineteen remaining focus checks on same source128; no previously executed check repeated')
def save():
    OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
env = os.environ.copy()
env['PATH'] = str(RELEASE) + os.pathsep + env.get('PATH', '')
env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
for key in ['PYTHONPATH', 'PYTHONHOME', 'PYTHONOPTIMIZE', 'PYTHONIOENCODING', 'PYTHONPYCACHEPREFIX']:
    env.pop(key, None)
try:
    for name in names:
        source, expected = ROOT / ('tests/fixtures/core/' + name + '.py'), ROOT / ('tests/fixtures/expected/' + name + '.out')
        command = [str(RELEASE / 'xlang3.exe'), str(source)]
        stdout, stderr = DATA / ('lambda-eager-comprehension-capture-r4-focused-remaining-20261009-' + name + '.stdout.log'), DATA / ('lambda-eager-comprehension-capture-r4-focused-remaining-20261009-' + name + '.stderr.log')
        row = dict(name=name, command=command, timed=False)
        record['phases'].append(row)
        save()
        with stdout.open('xb') as out, stderr.open('xb') as err:
            child = subprocess.run(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL, stdout=out, stderr=err,
                                   timeout=90, creationflags=subprocess.CREATE_NO_WINDOW)
        row.update(exit_code=child.returncode, passed=child.returncode == 0 and not stderr.read_bytes() and stdout.read_bytes().replace(b'\r\n', b'\n') == expected.read_bytes().replace(b'\r\n', b'\n'),
                   stdout_log=stdout.name, stdout_sha256=sha(stdout), stderr_log=stderr.name, stderr_sha256=sha(stderr))
        save()
    record['status'] = 'remaining_focus_passed' if all(r['passed'] for r in record['phases']) else 'remaining_focus_failed'
finally:
    record.update(terminal=True, hashes_after={p: sha(p) for p in before}, hashes_unchanged=all(sha(p) == h for p, h in before.items()) and tree(ROOT / 'build-repro/Release') == build['baseline_sha256'])
    save()
print(record['status'], 'hashstable', record['hashes_unchanged'], sha(OUT))
for row in record['phases']:
    print(row['name'], row['exit_code'], row['passed'])
raise SystemExit(0 if record['status'] == 'remaining_focus_passed' and record['hashes_unchanged'] else 1)
