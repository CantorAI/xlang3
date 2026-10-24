"""Finish normal R5 fixture checks after the dump-IR diagnostic-message harness stop."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CP = Path('C:/Python/Python314/python.exe')
OUT = DATA / 'lambda-eager-comprehension-capture-r5-focused-complete-20261009.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7)
assert sys.flags.isolated and not sys.flags.optimize and not OUT.exists()
build_path = DATA / 'lambda-eager-comprehension-capture-r5-build-terminal-20261009.json'
initial_path = DATA / 'lambda-eager-comprehension-capture-r5-focused-20261009.json'
assert sha(build_path) == '5a18941f71560ec98879872a0039703df19ad4856cd8225e375f3a3302b41fea'
assert sha(initial_path) == '24edd8537766760a12dde6b8f0591cd423e8b8426b76291c220af2999fb7c049'
build, initial = json.loads(build_path.read_bytes()), json.loads(initial_path.read_bytes())
assert initial['terminal'] and initial['hashes_unchanged'] and len(initial['phases']) == 3
assert initial['source_sha256'] == build['source_sha256'] and initial['binaries_sha256'] == build['binaries_sha256']
assert initial['build_receipt_sha256'] == sha(build_path)
for row in initial['phases']:
    assert row['exit_code'] == 0
    for stream in ['stdout', 'stderr']:
        assert sha(DATA / row[stream + '_log']) == row[stream + '_sha256']
for row in initial['phases'][:2]:
    assert row['passed'] and not (DATA / row['stderr_log']).read_bytes()
assert [r['name'] for r in initial['phases'][:2]] == ['cpp-interpreter', 'cpp-ir-codec']
strict = initial['phases'][2]
strict_ir = ROOT / 'scratch/performance/lambda-capture-r5-emitted-ir-20261009/lambda_eager_comprehension_capture/lambda_eager_comprehension_capture.ir.txt'
assert strict['name'] == 'lambda_eager_comprehension_capture' and '--dump-ir' in strict['command']
assert (DATA / strict['stderr_log']).read_bytes() == ('debug: wrote IR ' + str(strict_ir) + '\n').encode()
assert (DATA / strict['stdout_log']).read_bytes().replace(b'\r\n', b'\n') == (ROOT / 'tests/fixtures/expected/lambda_eager_comprehension_capture.out').read_bytes().replace(b'\r\n', b'\n')
names = ['lambda_eager_comprehension_capture', 'comprehension_target_lifetime',
 'comprehension_iterable_scope', 'nested_comprehension_capture', 'nested_generator_comprehension_capture',
 'compile_ast_list_comprehension', 'compile_ast_set_dict_comprehension', 'inline_property_dynamic_attr',
 'ordinary_canonical_slot_constructor', 'function_code_replacement', 'class_method_annotation_capture',
 'closure_cell_semantics', 'nonlocal_counter', 'private_slots_mangling', 'private_generator_name_mangling',
 'annotation_lambda_ast', 'nested_comprehensions', 'comprehension_multiple_filters',
 'compile_ast_lambda_generator', 'generator_expressions']
before = {str(ROOT / n): h for n, h in build['source_sha256'].items()}
before.update({str(RELEASE / n): h for n, h in build['binaries_sha256'].items()})
for path in [build_path, initial_path, strict_ir, Path(__file__)]:
    before[str(path)] = sha(path)
focus_inputs = {}
for name in names:
    for path in [ROOT / ('tests/fixtures/core/' + name + '.py'), ROOT / ('tests/fixtures/expected/' + name + '.out')]:
        before[str(path)] = sha(path)
        focus_inputs[str(path.relative_to(ROOT))] = sha(path)
assert all(sha(p) == h for p, h in before.items())
assert tree(ROOT / 'build-repro/Release') == build['baseline_sha256']
record = dict(status='running', terminal=False, source_sha256=build['source_sha256'],
 source_count=128, source_inventory_sha256=build['source_inventory_sha256'],
 build_receipt_sha256=sha(build_path), binaries_sha256=build['binaries_sha256'],
 candidate_binary_sha256=build['candidate_binary_sha256'], focus_inputs_sha256=focus_inputs,
 controller_sha256=sha(__file__), phases=initial['phases'][:2],
 initial_focus_receipt_sha256=sha(initial_path), prior_cpp_checks_reused_same_exact_candidate=True,
 controller_correction='Normal dump-IR stderr is a CLI diagnostic. Prior strict exit0/eight groups validated; rerun strict without dump, run remaining19 normal fixtures. Original failed harness receipt retained.',
 full_correctness_pending=True, performance_validation_pending=True)
def save():
    OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
env = os.environ.copy()
env['PATH'] = str(RELEASE) + os.pathsep + env.get('PATH', '')
env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
for k in ['PYTHONPATH', 'PYTHONHOME', 'PYTHONOPTIMIZE', 'PYTHONIOENCODING', 'PYTHONPYCACHEPREFIX']:
    env.pop(k, None)
try:
    for name in names:
        source = ROOT / ('tests/fixtures/core/' + name + '.py')
        expected = ROOT / ('tests/fixtures/expected/' + name + '.out')
        command = [str(RELEASE / 'xlang3.exe'), str(source)]
        stdout = DATA / ('lambda-eager-comprehension-capture-r5-focused-complete-20261009-' + name + '.stdout.log')
        stderr = DATA / ('lambda-eager-comprehension-capture-r5-focused-complete-20261009-' + name + '.stderr.log')
        row = dict(name=name, command=command, timed=False, passed=False)
        record['phases'].append(row)
        save()
        with stdout.open('xb') as o, stderr.open('xb') as e:
            child = subprocess.run(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL, stdout=o, stderr=e,
                                   timeout=90, creationflags=subprocess.CREATE_NO_WINDOW)
        row.update(exit_code=child.returncode, stdout_log=stdout.name, stdout_sha256=sha(stdout),
                   stderr_log=stderr.name, stderr_sha256=sha(stderr))
        assert child.returncode == 0 and not stderr.read_bytes(), (name, child.returncode, stderr.read_bytes())
        assert stdout.read_bytes().replace(b'\r\n', b'\n') == expected.read_bytes().replace(b'\r\n', b'\n'), name
        row['passed'] = True
        save()
    record['status'] = 'targeted_correctness_passed'
except BaseException as error:
    record.update(status='targeted_correctness_failed', error=repr(error))
finally:
    record.update(terminal=True, hashes_unchanged=all(sha(p) == h for p, h in before.items())
                  and tree(ROOT / 'build-repro/Release') == build['baseline_sha256'])
    save()
print(record['status'], len(record['phases']), 'hashstable', record['hashes_unchanged'], sha(OUT))
print(record.get('error', ''))
raise SystemExit(0 if record['status'] == 'targeted_correctness_passed' and record['hashes_unchanged'] else 1)
