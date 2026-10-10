"""Root numeric-IR experiment: exact inputs, fixed control, no configuration or retry."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path('D:/CantorAI/xlang3')
CP = Path('C:/Python/Python314/python.exe')
DATA = ROOT / 'doc/performance/data'
CONTROL = ROOT / 'build-repro/controls/gc-generic-cycles-r7b-accepted-20261009'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BASELINE = ROOT / 'build-repro/Release'
PREFIX = 'two-argument-double-ir-plan-r2-trial-20261010'
PROOF = ROOT / 'scratch/performance/two-argument-double-ir-plan-engine-r2-fixture-r3-proposed-20261009-provenance.json'
COMMAND = ROOT / 'scratch/performance/build-python-new-vm-continuation-r4-root-20261009.cmd'
PS_SHA = '30d462945ab4f8b546269b4921df20688efa033cf16eb8a85a1e9c4f3297050e'
DEBUG_SHA = 'dae4af562bc1d84a6983d91d5a7d4a0d96d000855aebfa07e872a8710d2221e8'
APP = DATA / (PREFIX + '-application.json')
BUILD = DATA / (PREFIX + '-build.json')

def sha(p):
    with Path(p).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def tree(p):
    return {f.relative_to(p).as_posix(): sha(f) for f in sorted(p.rglob('*')) if f.is_file()}

def write(p, record):
    p.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')

def check(root, mapping):
    assert all(sha(root / p) == h for p, h in mapping.items())

def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, check=True, capture_output=True).stdout

def shared():
    assert sys.version_info[:3] == (3, 14, 7) and not sys.flags.optimize
    assert Path(sys.executable).resolve() == CP.resolve()
    assert sha(CONTROL / 'provenance.json') == 'aa2d9f715ad11e78ffa58024c10631c37e7d83d816c4a8cb0523f6469287547f'
    control = json.loads((CONTROL / 'provenance.json').read_bytes())
    assert control['terminal'] and control['full_validated'] and control['fixed_gate_passed']
    check(CONTROL / 'sources', control['source_sha256'])
    assert tree(CONTROL / 'Release') == control['release_sha256']
    assert tree(BASELINE) == control['fixed_baseline_sha256']
    check(ROOT, control['unowned_tracked_dirty_sha256'])
    assert sha(PROOF) == '6a6a7a42f6564e0905454206060c9a25449c0ee89e45050618ecb73a38eca26f'
    proof = json.loads(PROOF.read_bytes())
    assert sha(ROOT / proof['patch']) == proof['patch_sha256'] == 'da97037a0d0ae7c764b17bcc9e4784b36f7b73265a8186f268e57c0f95ad8bfa'
    for p, h in proof['candidate_sha256'].items():
        assert sha(ROOT / proof['candidate_root'] / p) == h
    assert sha(ROOT / proof['fixture']) == proof['fixture_sha256']
    assert sha(ROOT / proof['proposed_expected_output']) == proof['proposed_expected_sha256']
    assert sha(ROOT / 'tests/cli/run_debugpy_launch_smoke.py') == DEBUG_SHA
    assert sha(COMMAND) == '2efd6c0b5662ca4cef35bd337077ecb3a13ad95e919c8e42ca4bcbd99a62dc8b'
    return control, proof

def apply():
    control, proof = shared()
    assert not APP.exists() and not BUILD.exists()
    assert tree(RELEASE) == control['release_sha256']
    source_before = dict(control['source_sha256'])
    source_before['tests/run_fixtures.ps1'] = PS_SHA
    check(ROOT, source_before)
    check(ROOT, proof['input_sha256'])
    assert git('rev-parse', 'HEAD').decode().strip() == '46496cf5ddb903e25927cce616c29c2f81032566'
    assert not git('diff', '--cached', '--name-only').strip()
    reference = DATA / 'two-argument-double-ir-plan-r3-reference-20261010.json'
    assert sha(reference) == '982014bca87d3712a170c0fcc455701555746864cb614ecd08afd27dd092fe58'
    assert json.loads(reference.read_bytes())['status'] == 'semantic_reference_passed'
    added = ['tests/fixtures/core/two_argument_double_ir_plan.py',
             'tests/fixtures/expected/two_argument_double_ir_plan.out']
    assert all(not (ROOT / p).exists() for p in added)
    changed = list(proof['owned_engine_paths']) + ['tests/run_fixtures.py', 'tests/run_fixtures.ps1']
    backup = ROOT / 'scratch/performance/two-argument-ir-trial-20261010-source-backup'
    if backup.exists():
        # A prior preflight stopped before live writes on a newline assertion.
        # Reuse its exact source backup rather than creating another directory.
        assert tree(backup) == {p: sha(ROOT / p) for p in changed}
    else:
        for p in changed:
            target = backup / p
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / p, target)
    # Prepare every candidate byte before the first live write.
    writes = {p: (ROOT / proof['candidate_root'] / p).read_bytes() for p in proof['owned_engine_paths']}
    py = (ROOT / 'tests/run_fixtures.py').read_bytes()
    ps = (ROOT / 'tests/run_fixtures.ps1').read_bytes()
    py_eol = b'\r\n' if b'\r\n' in py else b'\n'
    ps_eol = b'\r\n' if b'\r\n' in ps else b'\n'
    py_marker = b'gc_generic_cycles' + py_eol
    ps_marker = b'    "gc_generic_cycles",' + ps_eol
    assert py.count(py_marker) == 1 and ps.count(ps_marker) == 1
    writes['tests/run_fixtures.py'] = py.replace(py_marker, py_marker + b'two_argument_double_ir_plan' + py_eol)
    writes['tests/run_fixtures.ps1'] = ps.replace(ps_marker, ps_marker + b'    "two_argument_double_ir_plan",' + ps_eol)
    writes[added[0]] = (ROOT / proof['fixture']).read_bytes()
    writes[added[1]] = (ROOT / proof['proposed_expected_output']).read_bytes()
    record = dict(terminal=False, passed=False, status='applying', head=git('rev-parse', 'HEAD').decode().strip(),
                  controller_sha256=sha(__file__), accepted_control_manifest_path=str(CONTROL / 'provenance.json'),
                  accepted_control_manifest_sha256=sha(CONTROL / 'provenance.json'),
                  trial_patch_path=str(ROOT / proof['patch']), trial_patch_sha256=proof['patch_sha256'],
                  proposal_path=str(PROOF), proposal_sha256=sha(PROOF),
                  allowed_changed_source_paths=changed, added_source_paths=added,
                  before_source_sha256=source_before, before_release_sha256=control['release_sha256'],
                  source_backup_path=str(backup), source_backup_sha256={p: sha(backup / p) for p in changed},
                  fixed_baseline_sha256=control['fixed_baseline_sha256'],
                  unowned_tracked_dirty_sha256=control['unowned_tracked_dirty_sha256'],
                  harness_overlay_sha256={'tests/run_fixtures.ps1': PS_SHA},
                  protected_test_input_sha256={'tests/cli/run_debugpy_launch_smoke.py': DEBUG_SHA},
                  reference_path=str(reference), reference_sha256=sha(reference))
    write(APP, record)
    try:
        for p, content in writes.items():
            (ROOT / p).write_bytes(content)
        sources = {p: sha(ROOT / p) for p in source_before.keys() | set(added)}
        assert {p for p in source_before if sources[p] != source_before[p]} == set(changed)
        check(ROOT, proof['candidate_sha256'])
        check(ROOT, control['unowned_tracked_dirty_sha256'])
        assert tree(RELEASE) == control['release_sha256'] and tree(BASELINE) == control['fixed_baseline_sha256']
        record.update(terminal=True, passed=True, status='applied', source_sha256=sources, source_count=len(sources))
    except BaseException as error:
        for p in changed:
            shutil.copyfile(backup / p, ROOT / p)
        for p in added:
            if (ROOT / p).exists():
                (ROOT / p).unlink()
        record.update(terminal=True, status='application_failed_and_restored', error=repr(error))
        write(APP, record)
        raise
    write(APP, record)
    print('Applied numeric R2:', len(sources), 'source pins; application', sha(APP), flush=True)

def build(expected_app_sha):
    control, proof = shared()
    assert sha(APP) == expected_app_sha and not BUILD.exists()
    app = json.loads(APP.read_bytes())
    assert app['terminal'] and app['passed'] and app['status'] == 'applied'
    check(ROOT, app['source_sha256'])
    assert tree(RELEASE) == control['release_sha256']
    log = DATA / (PREFIX + '-build.log')
    assert not log.exists()
    command = ['cmd.exe', '/d', '/c', str(COMMAND)]
    record = dict(terminal=False, passed=False, status='building', command=command,
                  application_path=str(APP), application_sha256=sha(APP), source_sha256=app['source_sha256'],
                  controller_sha256=sha(__file__), build_command_source_sha256=sha(COMMAND),
                  started_at_unix=time.time())
    write(BUILD, record)
    child = None
    try:
        with log.open('xb') as stream:
            child = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.DEVNULL, stdout=stream,
                                     stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
            record['pid'] = child.pid
            write(BUILD, record)
            print('Build started at fixed path, PID', child.pid, flush=True)
            record['exit_code'] = child.wait(timeout=1800)
        check(ROOT, app['source_sha256'])
        check(ROOT, app['protected_test_input_sha256'])
        check(ROOT, control['unowned_tracked_dirty_sha256'])
        assert tree(BASELINE) == control['fixed_baseline_sha256']
        record.update(sources_unchanged=True, passed=record['exit_code'] == 0,
                      status='build_passed' if record['exit_code'] == 0 else 'build_failed')
    except BaseException as error:
        record.update(status='build_failed_or_invalid', error=repr(error))
    finally:
        if child is not None and child.poll() is None:
            subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=15)
            child.wait(timeout=15)
        record.update(terminal=True, owned_child_cleanup_completed=child is not None and child.poll() is not None,
                      release_sha256=tree(RELEASE), finished_at_unix=time.time(), log=log.name, log_sha256=sha(log))
        if not record['owned_child_cleanup_completed']:
            record.update(passed=False, status='cleanup_failed')
        write(BUILD, record)
    print(record['status'], sha(BUILD), flush=True)
    return 0 if record['passed'] else 1

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=['apply', 'build'])
    parser.add_argument('--application-sha256')
    args = parser.parse_args()
    if args.mode == 'apply':
        apply()
    else:
        raise SystemExit(build(args.application_sha256))
