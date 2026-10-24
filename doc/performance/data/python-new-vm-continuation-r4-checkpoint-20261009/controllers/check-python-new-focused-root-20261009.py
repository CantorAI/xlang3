"""Untimed constructor/lifetime/observer checks against an actual build receipt."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CONTROL = ROOT / 'build-repro/controls/lambda-eager-comprehension-capture-accepted-r5-20261009'
APP = DATA / 'python-new-vm-continuation-r4-applied-source-20261009.json'
BUILD = DATA / 'python-new-vm-continuation-r4-build-20261009.json'
NAMES = ('python_new_vm_continuation,python_new_completion_finalizer,call_ex_cross_activation_constructor,'
         'inherited_call_ex_constructor,ordinary_canonical_slot_constructor,call_ex_constructor_cache,'
         'explicit_slot_descriptor_fallback,synchronous_class_argument_lifetime,class_namespace_lifetime,'
         'slot_descriptor_owner,nested_profile_setting,debug_trace_profile_edges,trace_local_and_exception,'
         'sys_monitoring_all_events,generator_result_release,generator_resume_state_reuse,'
         'saved_frame_recursion_limit,module_class_constructor_frame_growth,call_default_positional_frame_fastpath').split(',')
PREFIX = 'python-new-vm-continuation-r4-focused-20261009'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}
parser = argparse.ArgumentParser()
parser.add_argument('--build-sha256', required=True)
args = parser.parse_args()
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated and not sys.flags.optimize
assert sha(APP) == '2c89133fbe3c3a4c0d13a1652b62362d69be1c79b07e197186682eac664cb0a8'
assert sha(BUILD) == args.build_sha256
app, build = json.loads(APP.read_bytes()), json.loads(BUILD.read_bytes())
assert build['terminal'] and build['passed'] and build['exit_code'] == 0
assert build['source_sha256'] == app['source_sha256'] and build['application_sha256'] == sha(APP)
assert build['source_unchanged'] and build['accepted_control_unchanged'] and build['fixed_baseline_unchanged']
assert build['log_sha256'] == sha(DATA / build['log'])
manifest = CONTROL / 'preserved-release-provenance.json'
assert sha(manifest) == '9271314856ede7f173396b2f39449a01d785f61a1579ce5f93c7786a2961dfad'
saved = json.loads(manifest.read_bytes())
assert not any(DATA.glob(PREFIX + '*'))
source_map = app['source_sha256'] | app['unowned_tracked_dirty_sha256']
assert all(sha(ROOT / n) == h for n, h in source_map.items())
assert tree(RELEASE) == build['binaries_sha256']
assert tree(CONTROL / 'Release') == saved['files_sha256']
assert tree(ROOT / 'build-repro/Release') == saved['fixed_baseline_sha256']
inputs = {str(p): sha(p) for p in (APP, BUILD, manifest, Path(__file__))}
for name in NAMES:
    for kind, suffix in (('core', 'py'), ('expected', 'out')):
        p = ROOT / 'tests/fixtures' / kind / (name + '.' + suffix)
        inputs[str(p)] = sha(p)
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = 'C:/Python/Python314/Lib'
phases = []
normalize = lambda b: b.decode('utf-8', errors='replace').replace('\r\n', '\n').rstrip()
for name in NAMES:
    command = [str(RELEASE / 'xlang3.exe'), str(ROOT / 'tests/fixtures/core' / (name + '.py'))]
    stdout, stderr = (DATA / (PREFIX + '-' + name + '.' + suffix) for suffix in ('stdout.log', 'stderr.log'))
    # Real files avoid waiting on inherited pipe handles after the direct child exits.
    with stdout.open('wb') as out_stream, stderr.open('wb') as err_stream:
        child = subprocess.run(command, cwd=ROOT, env=env, stdout=out_stream, stderr=err_stream, timeout=60)
    actual = normalize(stdout.read_bytes()).replace(str(ROOT / 'tests'), 'tests').replace('tests\\fixtures\\core\\', 'tests/fixtures/core/')
    expected = normalize((ROOT / 'tests/fixtures/expected' / (name + '.out')).read_bytes()).replace('tests\\fixtures\\core\\', 'tests/fixtures/core/')
    passed = child.returncode == 0 and actual == expected
    phases.append(dict(name=name, command=command, exit_code=child.returncode, passed=passed,
                       stdout=stdout.name, stdout_sha256=sha(stdout), stderr=stderr.name, stderr_sha256=sha(stderr)))
    print(('PASS ' if passed else 'FAIL ') + name, flush=True)
    if not passed:
        print('Actual:', repr(actual), 'Expected:', repr(expected), flush=True)
        break
stable = (all(sha(ROOT / n) == h for n, h in source_map.items()) and
          all(sha(Path(n)) == h for n, h in inputs.items()) and tree(RELEASE) == build['binaries_sha256'] and
          tree(CONTROL / 'Release') == saved['files_sha256'] and
          tree(ROOT / 'build-repro/Release') == saved['fixed_baseline_sha256'])
passed = len(phases) == len(NAMES) and all(p['passed'] for p in phases) and stable
record = dict(status='focused_passed' if passed else 'focused_failed', terminal=True, passed=passed,
              application_sha256=sha(APP), build_sha256=sha(BUILD), controller_sha256=sha(__file__),
              source_count=132, source_sha256=app['source_sha256'], binaries_sha256=build['binaries_sha256'],
              hashes_before=inputs, hashes_after={n: sha(Path(n)) for n in inputs}, hashes_unchanged=stable,
              expected_names=NAMES, phases=phases, timed=False, performance_validation_pending=True)
out = DATA / (PREFIX + '.json')
out.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print(record['status'], len(phases), '/', len(NAMES), 'receipt', sha(out), flush=True)
raise SystemExit(0 if passed else 1)
