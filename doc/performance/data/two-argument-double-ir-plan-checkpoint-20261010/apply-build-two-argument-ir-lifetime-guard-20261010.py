"""Integrate one reviewed lifetime guard, preserving every earlier receipt."""
import argparse
import difflib
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
OLD_APP = DATA / 'two-argument-double-ir-plan-r2-trial-20261010-application.json'
OLD_BUILD = DATA / 'two-argument-double-ir-plan-r2-trial-20261010-build.json'
OLD_PROOF = ROOT / 'scratch/performance/two-argument-double-ir-plan-engine-r2-fixture-r3-proposed-20261009-provenance.json'
COMMAND = ROOT / 'scratch/performance/build-python-new-vm-continuation-r4-root-20261009.cmd'
PREFIX = 'two-argument-double-ir-plan-r3-trial-20261010'
APP = DATA / (PREFIX + '-application.json')
BUILD = DATA / (PREFIX + '-build.json')
MERGED = ROOT / 'scratch/performance/two-argument-double-ir-plan-engine-r3-fixture-r3-proposed-20261010-provenance.json'
HEAD = '46496cf5ddb903e25927cce616c29c2f81032566'

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def tree(path):
    return {p.relative_to(path).as_posix(): sha(p) for p in sorted(path.rglob('*')) if p.is_file()}

def check(directory, mapping):
    assert all(sha(directory / p) == h for p, h in mapping.items())

def document(path):
    return json.loads(Path(path).read_bytes())

def write(path, value):
    with Path(path).open('xb') as stream:
        stream.write((json.dumps(value, indent=2) + '\n').encode())

def preflight():
    assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CP.resolve()
    assert sys.flags.isolated and not sys.flags.optimize
    assert sha(OLD_APP) == 'a68a609265662e89d8657a13bf00fde0ca174e4661122a7ab0e810705a4ecaac'
    assert sha(OLD_BUILD) == 'ba14f17d762bf33ad703cf31914b2feb06d0bc938d8774a7c8a858cf4aa79f9e'
    assert sha(OLD_PROOF) == '6a6a7a42f6564e0905454206060c9a25449c0ee89e45050618ecb73a38eca26f'
    assert sha(CONTROL / 'provenance.json') == 'aa2d9f715ad11e78ffa58024c10631c37e7d83d816c4a8cb0523f6469287547f'
    assert sha(COMMAND) == '2efd6c0b5662ca4cef35bd337077ecb3a13ad95e919c8e42ca4bcbd99a62dc8b'
    old, built, parent = document(OLD_APP), document(OLD_BUILD), document(CONTROL / 'provenance.json')
    assert old['passed'] and built['passed'] and parent['full_validated'] and parent['fixed_gate_passed']
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT).decode().strip() == HEAD
    assert not subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT).strip()
    check(ROOT, old['unowned_tracked_dirty_sha256'])
    check(ROOT, old['protected_test_input_sha256'])
    assert tree(BASELINE) == old['fixed_baseline_sha256']
    check(CONTROL / 'sources', parent['source_sha256'])
    assert tree(CONTROL / 'Release') == parent['release_sha256']
    return old, built, parent

def apply(delta_path, delta_sha):
    old, built, parent = preflight()
    assert not APP.exists() and not BUILD.exists() and not MERGED.exists()
    check(ROOT, old['source_sha256'])
    assert tree(RELEASE) == built['release_sha256']
    assert sha(delta_path) == delta_sha
    delta = document(delta_path)
    assert sha(ROOT / delta['patch']) == delta['patch_sha256']
    before = delta['raw_before_sha256']
    after = delta['candidate_sha256']
    assert set(before) == set(after) and set(before) <= set(old['source_sha256'])
    assert all(before[p] == old['source_sha256'][p] for p in before)
    check(ROOT / delta['candidate_root'], after)
    check(ROOT, delta.get('additional_implementation_pin', delta.get('implementation_pin', {})))
    paths = sorted(set(document(OLD_PROOF)['owned_engine_paths']) | set(after))
    candidate_root = ROOT / 'scratch/performance/two-argument-double-ir-plan-engine-r3-proposed-20261010-candidates'
    raw_root = ROOT / 'scratch/performance/two-argument-double-ir-plan-engine-r3-proposed-20261010-raw-inputs'
    assert not candidate_root.exists() and not raw_root.exists()
    patch_lines = []
    for name in paths:
        content = (ROOT / delta['candidate_root'] / name).read_bytes() if name in after else (ROOT / name).read_bytes()
        target = candidate_root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
        original = CONTROL / 'sources' / name
        assert sha(original) == old['before_source_sha256'][name]
        raw = raw_root / name
        raw.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, raw)
        patch_lines.append('diff --git a/' + name + ' b/' + name + '\n')
        patch_lines.extend(difflib.unified_diff(original.read_text().splitlines(keepends=True),
            content.decode().replace('\r\n', '\n').splitlines(keepends=True), 'a/' + name, 'b/' + name))
    patch = ROOT / 'scratch/performance/two-argument-double-ir-plan-engine-r3-proposed-20261010.patch'
    assert not patch.exists()
    patch.write_text(''.join(patch_lines), encoding='utf-8', newline='\n')
    proof = document(OLD_PROOF)
    proof.update(schema='two-argument-double-ir-plan-engine-r3-fixture-r3',
        status='frozen_merged_lifetime_guard_pending_root_validation',
        patch=patch.relative_to(ROOT).as_posix(), patch_sha256=sha(patch),
        candidate_root=candidate_root.relative_to(ROOT).as_posix(),
        raw_input_root=raw_root.relative_to(ROOT).as_posix(),
        owned_engine_paths=paths,
        input_sha256={p: old['before_source_sha256'][p] for p in paths},
        candidate_sha256={p: sha(candidate_root / p) for p in paths},
        previous_full_proposal_path=str(OLD_PROOF), previous_full_proposal_sha256=sha(OLD_PROOF),
        lifetime_guard_proposal_path=str(delta_path), lifetime_guard_proposal_sha256=delta_sha,
        live_owned_input_bytes_unchanged=False,
        live_state_basis='R2 trial preserved separately; full merged input bytes are accepted control source files.',
        full_patch_controller_sha256=sha(__file__),
        validation_limits=['Fresh guarded build, correctness, original official benchmark and full fixed gate required.'])
    write(MERGED, proof)
    archive = ROOT / 'build-repro/controls/two-argument-ir-r2-unaccepted-20261010'
    assert not archive.exists()
    for name in old['source_sha256']:
        target = archive / 'sources' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, target)
    assert tree(archive / 'sources') == old['source_sha256']
    shutil.copytree(RELEASE, archive / 'Release')
    assert tree(archive / 'Release') == built['release_sha256']
    write(archive / 'provenance.json', dict(terminal=True, accepted=False,
        reason='Observable lifetime difference; archived solely for trial reconstruction',
        source_sha256=old['source_sha256'], delta_before_source_sha256=before,
        release_sha256=built['release_sha256'],
        application_sha256=sha(OLD_APP), build_sha256=sha(OLD_BUILD)))
    for name in before:
        (ROOT / name).write_bytes((candidate_root / name).read_bytes())
    sources = {p: sha(ROOT / p) for p in old['source_sha256']}
    assert {p for p in sources if sources[p] != old['source_sha256'][p]} == set(before)
    check(ROOT, old['unowned_tracked_dirty_sha256'])
    record = dict(old)
    record.update(controller_sha256=sha(__file__), proposal_path=str(MERGED), proposal_sha256=sha(MERGED),
        trial_patch_path=str(patch), trial_patch_sha256=sha(patch),
        source_sha256=sources, source_count=len(sources),
        allowed_changed_source_paths=paths + ['tests/run_fixtures.py', 'tests/run_fixtures.ps1'],
        previous_application_path=str(OLD_APP), previous_application_sha256=sha(OLD_APP),
        previous_build_path=str(OLD_BUILD), previous_build_sha256=sha(OLD_BUILD),
        previous_trial_source_sha256=old['source_sha256'],
        delta_changed_source_paths=sorted(before), archive_path=str(archive),
        archive_manifest_sha256=sha(archive / 'provenance.json'),
        source_backup_path=str(archive / 'sources'), source_backup_sha256=old['source_sha256'],
        before_release_sha256=built['release_sha256'])
    write(APP, record)
    print('Guard applied', len(paths), 'engine paths /', len(sources), 'sources; application', sha(APP), flush=True)

def build(expected_app_sha):
    old, previous, parent = preflight()
    assert sha(APP) == expected_app_sha and not BUILD.exists()
    app = document(APP)
    assert app['controller_sha256'] == sha(__file__) and app['terminal'] and app['passed']
    check(ROOT, app['source_sha256'])
    assert tree(RELEASE) == app['before_release_sha256']
    log = DATA / (PREFIX + '-build.log')
    command = ['cmd.exe', '/d', '/c', str(COMMAND)]
    record = dict(terminal=False, passed=False, status='building', command=command,
        controller_sha256=sha(__file__), application_path=str(APP), application_sha256=sha(APP),
        source_sha256=app['source_sha256'], build_command_source_sha256=sha(COMMAND),
        started_at_unix=time.time(), owned_child_cleanup_completed=False)
    write(BUILD, record)
    child = None
    try:
        with log.open('xb') as stream:
            child = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.DEVNULL,
                stdout=stream, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
            record['pid'] = child.pid
            print('Build started at unchanged path, PID', child.pid, flush=True)
            record['exit_code'] = child.wait(timeout=1800)
        check(ROOT, app['source_sha256'])
        check(ROOT, app['unowned_tracked_dirty_sha256'])
        check(ROOT, app['protected_test_input_sha256'])
        assert tree(BASELINE) == app['fixed_baseline_sha256']
        record.update(sources_unchanged=True, passed=record['exit_code'] == 0,
            status='build_passed' if record['exit_code'] == 0 else 'build_failed',
            owned_child_cleanup_completed=child.poll() is not None)
    except BaseException as error:
        record.update(status='build_failed_or_invalid', error=repr(error), passed=False)
    finally:
        if child is not None and child.poll() is None:
            try:
                child.kill()  # Held primary process handle; never reconstruct a PID kill command.
                child.wait(timeout=15)
            except BaseException as error:
                record['cleanup_error'] = repr(error)
            # A killed primary does not prove all compiler descendants retired.
            record['owned_child_cleanup_completed'] = False
        record.update(terminal=True, finished_at_unix=time.time(),
            release_sha256=tree(RELEASE), log=log.name,
            log_sha256=sha(log) if log.exists() else None)
        BUILD.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(record['status'], sha(BUILD), flush=True)
    return 0 if record['passed'] else 1

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('apply', 'build'))
    parser.add_argument('--delta-proof', type=Path)
    parser.add_argument('--delta-sha256')
    parser.add_argument('--application-sha256')
    args = parser.parse_args()
    if args.mode == 'apply':
        assert args.delta_proof and args.delta_sha256
        apply(args.delta_proof.resolve(strict=True), args.delta_sha256)
    else:
        raise SystemExit(build(args.application_sha256))
