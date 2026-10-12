"""Record a root-confirmed terminal build, then run thirteen untimed compiler/capture checks."""
import argparse
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
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-exit-code', type=int, required=True)
parser.add_argument('--build-session-id', type=int, required=True)
args = parser.parse_args()
assert args.build_exit_code == 0 and args.build_session_id == 40897
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7)
assert sys.flags.isolated and not sys.flags.optimize
app_path = DATA / 'lambda-eager-comprehension-capture-r4-applied-source-20261009.json'
assert sha(app_path) == '05e005a9d3d5f913e64908e143a406433b5e7bfb5a65b35bb2670cd12eaea015'
app = json.loads(app_path.read_bytes())
sources = app['source_sha256']
assert app['source_count'] == len(sources) == 128
assert all(sha(ROOT / p) == h for p, h in sources.items())
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
parent_path = ROOT / 'build-repro/controls/lambda-eager-comprehension-capture-parent-20261009/preserved-release-provenance.json'
assert sha(parent_path) == '865ed0cfaed6e20bc47710808ebbe29cc56ed8d11426ba2e90e008490308b0e1'
parent = json.loads(parent_path.read_bytes())
baseline = tree(ROOT / 'build-repro/Release')
release = tree(RELEASE)
assert baseline == parent['fixed_baseline_sha256'] and len(release) == 178
build_path = DATA / 'lambda-eager-comprehension-capture-r4-build-terminal-20261009.json'
focused_path = DATA / 'lambda-eager-comprehension-capture-r4-focused-20261009.json'
assert not build_path.exists() and not focused_path.exists()
log = DATA / 'lambda-eager-comprehension-capture-r4-build-20261009.log'
assert 'Linking CXX shared library Release\\xlang3_runtime.dll' in log.read_text()
build = dict(status='build_passed', terminal=True, actual_root_tool_exit_code=args.build_exit_code,
             root_session_id=args.build_session_id, log=str(log.relative_to(ROOT)), log_sha256=sha(log),
             configuration='Release', build_directory=str(RELEASE.parent), source_inventory_sha256=sha(app_path),
             source_count=128, source_sha256=sources, binaries_sha256=release,
             candidate_binary_sha256=dict(exe=sha(RELEASE / 'xlang3.exe'), dll=sha(RELEASE / 'xlang3_runtime.dll')),
             baseline_sha256=baseline, performance_validation_pending=True)
build_path.write_text(json.dumps(build, indent=2) + '\n', encoding='utf-8')
names = ['lambda_eager_comprehension_capture', 'comprehension_target_lifetime',
         'comprehension_iterable_scope', 'nested_comprehension_capture',
         'nested_generator_comprehension_capture', 'compile_ast_list_comprehension',
         'compile_ast_set_dict_comprehension', 'inline_property_dynamic_attr',
         'ordinary_canonical_slot_constructor', 'function_code_replacement',
         'class_method_annotation_capture', 'closure_cell_semantics', 'nonlocal_counter',
         'private_slots_mangling', 'private_generator_name_mangling', 'annotation_lambda_ast',
         'nested_comprehensions', 'comprehension_multiple_filters', 'compile_ast_lambda_generator',
         'generator_expressions']
commands = [('cpp-interpreter', [str(RELEASE / 'xlang3_interpreter_tests.exe')], None),
            ('cpp-ir-codec', [str(RELEASE / 'xlang3_ir_codec_tests.exe')], None)]
focus_inputs = {}
for name in names:
    source, expected = ROOT / ('tests/fixtures/core/' + name + '.py'), ROOT / ('tests/fixtures/expected/' + name + '.out')
    focus_inputs[str(source.relative_to(ROOT))] = sha(source)
    focus_inputs[str(expected.relative_to(ROOT))] = sha(expected)
    commands.append((name, [str(RELEASE / 'xlang3.exe'), str(source)], expected.read_bytes()))
record = dict(status='preflight', terminal=False, source_count=128, source_sha256=sources,
              source_inventory_sha256=sha(app_path), build_receipt_sha256=sha(build_path),
              binaries_sha256=release, candidate_binary_sha256=build['candidate_binary_sha256'],
              controller_sha256=sha(Path(__file__)), focus_inputs_sha256=focus_inputs, phases=[],
              full_correctness_pending=True, performance_validation_pending=True)
def save():
    focused_path.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
env = os.environ.copy()
env['PATH'] = str(RELEASE) + os.pathsep + env.get('PATH', '')
env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
for key in ['PYTHONPATH', 'PYTHONOPTIMIZE', 'PYTHONIOENCODING', 'PYTHONPYCACHEPREFIX']:
    env.pop(key, None)
try:
    for name, command, expected in commands:
        stdout, stderr = DATA / ('lambda-eager-comprehension-capture-r4-focused-20261009-' + name + '.stdout.log'), DATA / ('lambda-eager-comprehension-capture-r4-focused-20261009-' + name + '.stderr.log')
        row = dict(name=name, command=command, passed=False, timed=False)
        record['phases'].append(row)
        save()
        with stdout.open('xb') as out, stderr.open('xb') as err:
            child = subprocess.run(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL, stdout=out, stderr=err,
                                   timeout=90, creationflags=subprocess.CREATE_NO_WINDOW)
        row.update(exit_code=child.returncode, stdout_log=stdout.name, stdout_sha256=sha(stdout),
                   stderr_log=stderr.name, stderr_sha256=sha(stderr))
        assert child.returncode == 0 and not stderr.read_bytes(), (name, child.returncode, stdout.read_bytes(), stderr.read_bytes())
        if expected is not None:
            assert stdout.read_bytes().replace(b'\r\n', b'\n') == expected.replace(b'\r\n', b'\n'), name
        row['passed'] = True
        save()
    record['status'] = 'targeted_correctness_passed'
except BaseException as error:
    record.update(status='targeted_correctness_failed', error=repr(error))
finally:
    record['hashes_unchanged'] = all(sha(ROOT / p) == h for p, h in (sources | focus_inputs).items()) and tree(RELEASE) == release and tree(ROOT / 'build-repro/Release') == baseline
    record['terminal'] = True
    save()
print(record['status'], 'phases', len(record['phases']), 'hashstable', record['hashes_unchanged'])
print('build_receipt_sha256', sha(build_path), 'focused_receipt_sha256', sha(focused_path))
if record['status'] != 'targeted_correctness_passed':
    print(record.get('error'))
raise SystemExit(0 if record['status'] == 'targeted_correctness_passed' and record['hashes_unchanged'] else 1)
