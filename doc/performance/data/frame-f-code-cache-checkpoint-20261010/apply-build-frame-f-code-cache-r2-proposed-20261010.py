"""Root-only cache integration over an explicitly unaccepted numeric parent."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path('D:/CantorAI/xlang3')
CP = Path('C:/Python/Python314/python.exe')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BASELINE = ROOT / 'build-repro/Release'
CONTROL = ROOT / 'build-repro/controls/gc-generic-cycles-r7b-accepted-20261009'
ARCHIVE = ROOT / 'build-repro/controls/frame-f-code-cache-numeric-unaccepted-parent-20261010'
OLD_APP = DATA / 'two-argument-double-ir-plan-r3-trial-20261010-application.json'
OLD_BUILD = DATA / 'two-argument-double-ir-plan-r3-trial-20261010-build.json'
PROPOSAL = ROOT / 'scratch/performance/frame-f-code-lazy-cache-r2-proposed-20261010-provenance.json'
REFERENCE = DATA / 'frame-f-code-r2-reference-20261010.json'
REFERENCE_CONTROLLER = ROOT / 'scratch/performance/check-frame-f-code-r2-reference-20261010.py'
COMMAND = ROOT / 'scratch/performance/build-python-new-vm-continuation-r4-root-20261009.cmd'
PREFIX = 'frame-f-code-cache-r2-trial-20261010'
APP = DATA / (PREFIX + '-application.json')
BUILD = DATA / (PREFIX + '-build.json')
HEAD = '46496cf5ddb903e25927cce616c29c2f81032566'
SUPPLEMENT = 'src/runtime/modules/system/weakref_module.cpp'
PINS = {
    OLD_APP: 'b6aec3660100c754eff3dea59a67ddd42f92842f57138cfe17fde7594181e02a',
    OLD_BUILD: '0e24b3ff4053f049c1febb43e49a22b3a9d8c99447839dac0e58af1b184eba0f',
    PROPOSAL: '4b7da282acf73b4eb3da569a28268b5c262fa95270c818d4d4f078f3555afbb0',
    REFERENCE: '04a8c326a4393ad9ab7ee2dfc3c5d97e45a1c32d643aa378347cca5abacdb029',
    REFERENCE_CONTROLLER: '7258d2a456ce2f8ef6fc8612d9ed939d91a7d5cdc181e1b3d0b07d19ea29fab6',
    CONTROL / 'provenance.json': 'aa2d9f715ad11e78ffa58024c10631c37e7d83d816c4a8cb0523f6469287547f',
    COMMAND: '2efd6c0b5662ca4cef35bd337077ecb3a13ad95e919c8e42ca4bcbd99a62dc8b',
    CP: '4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9',
    CP.parent / 'python314.dll': '0f9857ffdfe010fe6b99328d58c2e3c7472ce75f336bf9c2ad9bd5bca3bce700',
}

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def tree(directory):
    return {p.relative_to(directory).as_posix(): sha(p)
            for p in sorted(directory.rglob('*')) if p.is_file()}

def check(directory, mapping):
    for name, digest in mapping.items():
        path = directory / name
        assert path.resolve().is_relative_to(directory.resolve()), name
        assert sha(path) == digest, str(path)

def document(path):
    return json.loads(Path(path).read_bytes())

def write(path, value):
    with Path(path).open('xb') as stream:
        stream.write((json.dumps(value, indent=2) + '\n').encode())

def copy_map(origin, target, mapping):
    for name in mapping:
        destination = target / name
        assert destination.resolve().is_relative_to(target.resolve())
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(origin / name, destination)
    assert tree(target) == mapping

def common():
    assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated and not sys.flags.optimize
    assert Path(sys.executable).resolve() == CP.resolve()
    assert all(sha(path) == digest for path, digest in PINS.items())
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT).decode().strip() == HEAD
    assert not subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT).strip()
    old, built, parent, proposal, reference = map(document,
        (OLD_APP, OLD_BUILD, CONTROL / 'provenance.json', PROPOSAL, REFERENCE))
    assert old['terminal'] and old['passed'] and built['terminal'] and built['passed']
    assert built['owned_child_cleanup_completed'] and built['source_sha256'] == old['source_sha256']
    assert len(old['source_sha256']) == 145 and len(built['release_sha256']) == 178
    assert parent['full_validated'] and parent['fixed_gate_passed']
    assert tree(CONTROL / 'Release') == parent['release_sha256']
    check(CONTROL / 'sources', parent['source_sha256'])
    assert tree(BASELINE) == old['fixed_baseline_sha256'] == parent['fixed_baseline_sha256']
    assert len(old['fixed_baseline_sha256']) == 177
    check(ROOT, old['unowned_tracked_dirty_sha256'])
    check(ROOT, old['protected_test_input_sha256'])
    assert proposal['current_numeric_parent']['application_sha256'] == sha(OLD_APP)
    assert proposal['current_numeric_parent']['build_sha256'] == sha(OLD_BUILD)
    assert proposal['current_numeric_parent']['accepted_performance_claim'] is False
    assert reference['terminal'] and reference['inputs_unchanged'] and reference['reference_passed']
    assert reference['controller_sha256'] == sha(REFERENCE_CONTROLLER) and len(reference['results']) == 9
    for row in reference['results']:
        for kind in ('stdout', 'stderr'):
            assert sha(DATA / row[kind]) == row[kind + '_sha256']
        assert row['direct_child_waited']
        assert row['strict_expected_match'] == (row['runtime'] == 'cpython3147')
    return old, built, parent, proposal, reference

def apply():
    old, built, parent, proposal, reference = common()
    assert not APP.exists() and not BUILD.exists() and not ARCHIVE.exists()
    before = proposal['parent_recorded_source_sha256']
    assert len(before) == 146 and set(before) - set(old['source_sha256']) == {SUPPLEMENT}
    assert {p: before[p] for p in old['source_sha256']} == old['source_sha256']
    check(ROOT, before)
    assert tree(RELEASE) == built['release_sha256']
    assert all(sha(p) == h for p, h in reference['pins'].items())
    raw, after = proposal['raw_before_sha256'], proposal['candidate_source_sha256']
    added = set(proposal['new_owned_targets'])
    assert len(raw) == 6 and len(after) == 8 and len(added) == 2
    assert set(after) == set(raw) | added and not (set(raw) & added)
    assert all(before[p] == h for p, h in raw.items())
    assert all(not (ROOT / p).exists() for p in added)
    candidates = Path(proposal['candidate_root'])
    check(candidates, after)
    check(Path(proposal['raw_input_root']), raw)
    assert sha(proposal['patch']) == proposal['patch_sha256']
    expected = dict(before, **after)
    assert expected == proposal['candidate_recorded_source_sha256'] and len(expected) == 148
    numeric_owned = set(old['allowed_changed_source_paths']) | set(old['added_source_paths'])
    cumulative = sorted(numeric_owned | set(after))
    engines = sorted(p for p in cumulative if p.startswith('src/'))
    assert len(numeric_owned) == 8 and len(cumulative) == 13 and len(engines) == 7
    for runner in ('tests/run_fixtures.py', 'tests/run_fixtures.ps1'):
        text = (candidates / runner).read_text(encoding='utf-8')
        assert text.count('two_argument_double_ir_plan') == text.count('frame_f_code_cache') == 1
    record = dict(terminal=False, passed=False, status='preserving_unaccepted_parent',
        head=HEAD, controller_sha256=sha(__file__), proposal_path=str(PROPOSAL), proposal_sha256=sha(PROPOSAL),
        trial_patch_path=proposal['patch'], trial_patch_sha256=proposal['patch_sha256'],
        previous_application_path=str(OLD_APP), previous_application_sha256=sha(OLD_APP),
        previous_build_path=str(OLD_BUILD), previous_build_sha256=sha(OLD_BUILD),
        accepted_control_manifest_path=str(CONTROL / 'provenance.json'),
        accepted_control_manifest_sha256=sha(CONTROL / 'provenance.json'),
        fixed_baseline_sha256=old['fixed_baseline_sha256'],
        unowned_tracked_dirty_sha256=old['unowned_tracked_dirty_sha256'],
        protected_test_input_sha256=old['protected_test_input_sha256'],
        harness_overlay_sha256=old['harness_overlay_sha256'],
        original_recorded_source_sha256=old['source_sha256'], before_source_sha256=before,
        explicit_source_supplement_sha256={SUPPLEMENT: before[SUPPLEMENT]},
        before_release_sha256=built['release_sha256'], incremental_owned_targets=sorted(after),
        cumulative_owned_targets=cumulative, cumulative_owned_engine_paths=engines,
        reference_path=str(REFERENCE), reference_sha256=sha(REFERENCE), numeric_parent_accepted=False,
        inherited_correctness=False, performance_accepted=False, full_validated=False)
    write(APP, record)
    changed = []
    try:
        copy_map(ROOT, ARCHIVE / 'sources', before)
        shutil.copytree(RELEASE, ARCHIVE / 'Release')
        assert tree(ARCHIVE / 'Release') == built['release_sha256']
        copy_map(ROOT, ARCHIVE / 'unowned', old['unowned_tracked_dirty_sha256'])
        evidence = {str(REFERENCE): sha(REFERENCE), str(REFERENCE_CONTROLLER): sha(REFERENCE_CONTROLLER),
                    str(PROPOSAL): sha(PROPOSAL), proposal['patch']: proposal['patch_sha256']}
        for row in reference['results']:
            evidence.update({str(DATA / row[k]): row[k + '_sha256'] for k in ('stdout', 'stderr')})
        for index, (path, digest) in enumerate(evidence.items()):
            target = ARCHIVE / 'evidence' / (str(index) + '-' + Path(path).name)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
            assert sha(target) == digest
        write(ARCHIVE / 'provenance.json', dict(terminal=True, accepted=False,
            reason='Guarded numeric parent remains unaccepted; prior gate invalid. No correctness inherited.',
            original_recorded_source_sha256=old['source_sha256'], source_sha256=before,
            source_count=146, original_recorded_source_count=145, release_sha256=built['release_sha256'],
            fixed_baseline_sha256=old['fixed_baseline_sha256'], evidence_sha256=evidence,
            unowned_tracked_dirty_sha256=old['unowned_tracked_dirty_sha256'],
            application_sha256=sha(OLD_APP), build_sha256=sha(OLD_BUILD), controller_sha256=sha(__file__)))
        # Recheck the whole parent after preservation and before the first owned write.
        check(ROOT, before)
        assert tree(RELEASE) == built['release_sha256']
        for name in sorted(after):
            changed.append(name)
            (ROOT / name).write_bytes((candidates / name).read_bytes())
        check(ROOT, expected)
        assert {p for p in before if expected[p] != before[p]} == set(raw)
        common()
        assert tree(RELEASE) == built['release_sha256']
        record.update(passed=True, status='applied', source_sha256=expected, source_count=148,
            added_source_paths=sorted(added), allowed_changed_source_paths=sorted(raw),
            archive_path=str(ARCHIVE), archive_manifest_sha256=sha(ARCHIVE / 'provenance.json'),
            source_backup_path=str(ARCHIVE / 'sources'), source_backup_sha256=before,
            numeric_unchanged_paths=sorted(numeric_owned - set(after)))
    except BaseException as error:
        record.update(status='application_failed', error=repr(error), rollback_completed=False)
        try:
            for name in reversed(changed):
                if name in added:
                    (ROOT / name).unlink(missing_ok=True)
                else:
                    shutil.copyfile(ARCHIVE / 'sources' / name, ROOT / name)
            check(ROOT, before)
            assert all(not (ROOT / p).exists() for p in added)
            assert tree(RELEASE) == built['release_sha256']
            record['rollback_completed'] = True
        except BaseException as rollback_error:
            record['rollback_error'] = repr(rollback_error)
    finally:
        record.update(terminal=True, finished_at_unix=time.time())
        APP.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(record['status'], sha(APP), flush=True)
    return 0 if record['passed'] else 1

def build(expected_app_sha):
    common()
    assert sha(APP) == expected_app_sha and not BUILD.exists()
    app = document(APP)
    assert app['terminal'] and app['passed'] and app['controller_sha256'] == sha(__file__)
    assert app['source_count'] == 148 and app['numeric_parent_accepted'] is False
    check(ROOT, app['source_sha256'])
    assert tree(RELEASE) == app['before_release_sha256']
    check(ARCHIVE / 'sources', app['source_backup_sha256'])
    assert sha(ARCHIVE / 'provenance.json') == app['archive_manifest_sha256']
    log = DATA / (PREFIX + '-build.log')
    command = ['cmd.exe', '/d', '/c', str(COMMAND)]
    record = dict(terminal=False, passed=False, status='building', command=command,
        controller_sha256=sha(__file__), application_path=str(APP), application_sha256=sha(APP),
        source_sha256=app['source_sha256'], build_command_source_sha256=sha(COMMAND),
        started_at_unix=time.time(), owned_child_cleanup_completed=False, full_validated=False)
    write(BUILD, record)
    child = None
    try:
        with log.open('xb') as stream:
            child = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.DEVNULL, stdout=stream,
                stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
            record['pid'] = child.pid
            print('Build at unchanged fixed path, PID', child.pid, flush=True)
            record['exit_code'] = child.wait(timeout=1800)
        check(ROOT, app['source_sha256'])
        common()
        assert tree(ARCHIVE / 'Release') == app['before_release_sha256']
        release = tree(RELEASE)
        assert len(release) == 178
        record.update(sources_unchanged=True, passed=record['exit_code'] == 0,
            status='build_passed' if record['exit_code'] == 0 else 'build_failed',
            owned_child_cleanup_completed=child.poll() is not None)
    except BaseException as error:
        record.update(status='build_failed_or_invalid', error=repr(error), passed=False)
    finally:
        if child is not None and child.poll() is None:
            try:
                child.kill()  # Held primary handle only; never kill a foreign reconstructed PID.
                child.wait(timeout=15)
            except BaseException as error:
                record['cleanup_error'] = repr(error)
            record['owned_child_cleanup_completed'] = False  # Descendants not proven retired after timeout.
        record.update(terminal=True, finished_at_unix=time.time(), release_sha256=tree(RELEASE),
            log=log.name, log_sha256=sha(log) if log.exists() else None)
        BUILD.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(record['status'], sha(BUILD), flush=True)
    return 0 if record['passed'] else 1

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('apply', 'build'))
    parser.add_argument('--application-sha256')
    args = parser.parse_args()
    if args.mode == 'build':
        assert args.application_sha256
    raise SystemExit(apply() if args.mode == 'apply' else build(args.application_sha256))
