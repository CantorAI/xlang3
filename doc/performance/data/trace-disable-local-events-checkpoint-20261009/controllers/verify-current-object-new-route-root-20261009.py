"""Authenticate the current candidate/control for the prepared route diagnostic."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CONTROL = ROOT / 'build-repro/controls/lambda-eager-comprehension-capture-accepted-r5-20261009'
CORRECT = DATA / 'trace-disable-local-events-correctness-20261009.json'
PRIOR = DATA / 'object-new-route-untimed-20261009.json'
IR_PROOF = DATA / 'object-new-route-untimed-ir-verification-20261009.json'
CHILD = ROOT / 'scratch/performance/object-new-static-lookup-diagnostic-child-20261009.py'
PREFIX = 'object-new-route-current-verification-20261009'
OUT = DATA / (PREFIX + '.json')
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda d: {p.relative_to(d).as_posix(): sha(p) for p in d.rglob('*') if p.is_file()}
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated
for p, h in ((CORRECT, '617a0841153d9f7e9620c7b23fb5c00ab55ad4e5c81b930a792e7ec773dc3e98'),
    (PRIOR, 'bd6fd41c0f8f05952d529f6ac85c7a036dbe5684090815edef511edca13618c7'),
    (IR_PROOF, '8114c28bc2a6057d290e009d0b906c0e30816ba01466851216d6512e0b3038c8'),
    (CHILD, 'b1f32d611604d5ef67575bf1c6cdb4c6cd6f1bf7281aef37c13c228ef6ef2ee2'),
    (CONTROL / 'preserved-release-provenance.json', '9271314856ede7f173396b2f39449a01d785f61a1579ce5f93c7786a2961dfad')):
    assert sha(p) == h
correct, prior, ir_proof, control = (json.loads(p.read_bytes()) for p in
    (CORRECT, PRIOR, IR_PROOF, CONTROL / 'preserved-release-provenance.json'))
assert correct['status'] == 'correctness_passed_performance_pending' and correct['source_count'] == 137
assert tree(RELEASE) == correct['release_sha256'] and tree(CONTROL / 'Release') == control['files_sha256']
assert tree(CONTROL / 'source-snapshot') == control['source_snapshot_sha256']
assert ir_proof['status'] == 'untimed_semantics_and_ir_verified' and all(ir_proof['checks'].values())
# Preserve the inspected IR from the same diagnostic source. Compiler inputs
# have not changed: the intervening engine edit only changes trace dispatch.
changed = [p for p, h in correct['source_sha256'].items()
    if str(ROOT / p) in prior['pins_before'] and prior['pins_before'][str(ROOT / p)] != h]
assert set(changed) == {'src/executor/xlang_vm/xlang_vm_loop.cpp', 'tests/run_fixtures.py', 'tests/run_fixtures.ps1'}
prior_folder = DATA / 'object-new-route-untimed-20261009'
assert all(sha(prior_folder / p) == h for p, h in prior['artifacts_sha256'].items())
cp_phase = prior['phases'][0]
assert cp_phase['label'] == 'cpython-first' and cp_phase['exit_code'] == 0
assert cp_phase['semantic_result']['verify_only'] and cp_phase['semantic_result']['timed_operation_count'] == 0
assert all(sha(p) == h for p, h in cp_phase['semantic_result']['hashes_before'].items())
assert not any(DATA.glob(PREFIX + '*'))
pins = {str(ROOT / p): h for p, h in correct['source_sha256'].items()}
pins.update({str(RELEASE / p): h for p, h in correct['release_sha256'].items()})
pins.update({str(CONTROL / 'Release' / p): h for p, h in control['files_sha256'].items()})
pins.update({str(CONTROL / 'source-snapshot' / p): h for p, h in control['source_snapshot_sha256'].items()})
for p in (CP, CP.with_name('python314.dll'), CORRECT, PRIOR, IR_PROOF, CHILD, Path(__file__), CONTROL / 'preserved-release-provenance.json'):
    pins[str(p)] = sha(p)
assert all(sha(p) == h for p, h in pins.items())
env = os.environ.copy()
for k in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE', 'PYTHONHOME'):
    env.pop(k, None)
env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
record = {'terminal': False, 'status': 'verifying', 'timed': False, 'pins_before': pins, 'phases': [],
    'correctness_sha256': sha(CORRECT), 'source_count': 137,
    'cpython_reference': {'capture_sha256': sha(PRIOR), 'stdout_sha256': cp_phase['stdout_sha256']},
    'reused_ir_sha256': ir_proof['ir_sha256'], 'reused_ir_proof_sha256': sha(IR_PROOF),
    'ir_reuse_scope': 'Existing IR for identical child and unchanged recorded compiler inputs; not a fresh IR emission or a dynamic optimization-hit count.',
    'changed_recorded_inputs_since_ir': changed,
    'scope': 'Two fresh untimed allocation-wrapper checks, on accepted R5 control and current trace-disable candidate. Earlier authenticated CPython semantics and IR are retained. No timed loop or suite score.'}
def save():
    OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
try:
    for label, exe in (('xlang3-control', CONTROL / 'Release/xlang3.exe'), ('xlang3-current', RELEASE / 'xlang3.exe')):
        command = [str(exe), str(CHILD), '--selection', label, '--case-order', 'original_lookup,saved_native_lookup',
            '--runtime-executable', str(exe), '--exe-sha256', sha(exe), '--dll-sha256', sha(exe.with_name('xlang3_runtime.dll')),
            '--source-sha256', sha(CHILD), '--verify-only']
        stdout, stderr = DATA / (PREFIX + '-' + label + '.stdout.log'), DATA / (PREFIX + '-' + label + '.stderr.log')
        local = dict(env, PATH=str(exe.parent) + os.pathsep + env.get('PATH', ''))
        with stdout.open('xb') as out, stderr.open('xb') as err:
            child = subprocess.Popen(command, cwd=ROOT, env=local, stdin=subprocess.DEVNULL,
                stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                code = child.wait(timeout=60)
            finally:
                if child.poll() is None:
                    subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=10)
                    child.wait(timeout=10)
        row = {'label': label, 'command': command, 'exit_code': code, 'stdout': stdout.name,
            'stdout_sha256': sha(stdout), 'stderr': stderr.name, 'stderr_sha256': sha(stderr)}
        record['phases'].append(row)
        save()
        assert code == 0, stderr.read_text(errors='replace')[-2000:]
        result = json.loads(stdout.read_text(encoding='utf-8'))
        assert result['verify_only'] and result['timed_operation_count'] == 0 and result['seconds'] == {}
        assert result['hashes_unchanged'] and result['same_allocated_class'] and result['python_callable_identities_unchanged']
        row['result'] = result
        save()
        print(label + ': untimed PASS', flush=True)
    record['status'] = 'current_route_verification_passed'
except BaseException as error:
    record.update(status='current_route_verification_failed', error=repr(error))
finally:
    record['terminal'] = True
    record['pins_after'] = {p: sha(p) for p in pins}
    record['hashes_unchanged'] = record['pins_after'] == pins
    if not record['hashes_unchanged']: record['status'] = 'invalid_hash_drift'
    save()
print(json.dumps({'status': record['status'], 'receipt_sha256': sha(OUT), 'error': record.get('error')}, indent=2))
raise SystemExit(0 if record['status'] == 'current_route_verification_passed' else 1)
