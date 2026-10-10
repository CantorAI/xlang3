"""Build the bounded VM trial only after the frozen suite capture is terminal."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path('D:/CantorAI/xlang3')
CP = Path('C:/Python/Python314/python.exe')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CONTROL = ROOT / 'build-repro/controls/gc-generic-cycles-r7b-accepted-20261009'
CONTROL_SHA = 'aa2d9f715ad11e78ffa58024c10631c37e7d83d816c4a8cb0523f6469287547f'
COMMAND = ROOT / 'scratch/performance/build-python-new-vm-continuation-r4-root-20261009.cmd'
COMMAND_SHA = '2efd6c0b5662ca4cef35bd337077ecb3a13ad95e919c8e42ca4bcbd99a62dc8b'
CHANGED = {'src/executor/xlang_vm/xlang_vm_loop.cpp',
           'tests/run_fixtures.py', 'tests/run_fixtures.ps1'}
ADDED = {'tests/fixtures/core/vm_active_code_view.py',
         'tests/fixtures/expected/vm_active_code_view.out'}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    require(sys.version_info[:3] == (3, 14, 7) and not sys.flags.optimize
            and Path(sys.executable).resolve() == CP.resolve(), 'Use exact fixed CPython3.14.7')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application', type=Path, required=True)
    parser.add_argument('--application-sha256', required=True)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    require(args.prefix.startswith('vm-active-code-view-')
            and all(c.isalnum() or c in '-_' for c in args.prefix), 'Invalid trial prefix')
    application_path = args.application.resolve(strict=True)
    require(application_path.is_relative_to(DATA.resolve())
            and sha(application_path) == args.application_sha256, 'Application binding differs')
    app = json.loads(application_path.read_bytes())
    require(sha(CONTROL / 'provenance.json') == CONTROL_SHA, 'Accepted control manifest differs')
    control = json.loads((CONTROL / 'provenance.json').read_bytes())
    require(control['full_validated'] and control['fixed_gate_passed'], 'Control is not validated')
    terminal_path = Path(app['terminal_supplement_path']).resolve(strict=True)
    require(terminal_path.is_relative_to(DATA.resolve())
            and sha(terminal_path) == app['terminal_supplement_sha256'], 'Terminal capture binding differs')
    require(json.loads(terminal_path.read_bytes())['terminal'] is True, 'Benchmark capture is still live')
    require(Path(app['accepted_control_manifest_path']).resolve() == CONTROL / 'provenance.json'
            and app['accepted_control_manifest_sha256'] == CONTROL_SHA, 'Application control differs')
    require(set(app['allowed_changed_source_paths']) == CHANGED
            and set(app['added_source_paths']) == ADDED, 'Trial source scope differs')
    sources = app['source_sha256']
    require(set(sources) == set(control['source_sha256']) | ADDED
            and app['source_count'] == len(sources), 'Candidate source population differs')
    require({p for p, h in control['source_sha256'].items() if sources[p] != h} == CHANGED,
            'Unexpected changed source')
    require(app['fixed_baseline_sha256'] == control['fixed_baseline_sha256']
            and app['unowned_tracked_dirty_sha256'] == control['unowned_tracked_dirty_sha256'],
            'Fixed baseline or unowned changes differ')
    require(sha(COMMAND) == COMMAND_SHA, 'Established build command differs')
    patch = Path(app['trial_patch_path']).resolve(strict=True)
    require(sha(patch) == app['trial_patch_sha256']
            == '4dfd157ff5f3b356cab010be278233644eb19843b8327cd5dd76a56634eeb6a9', 'Trial patch differs')

    def unchanged():
        return (all(sha(ROOT / p) == h for p, h in sources.items())
                and all(sha(ROOT / 'build-repro/Release' / p) == h
                        for p, h in control['fixed_baseline_sha256'].items())
                and all(sha(ROOT / p) == h for p, h in control['unowned_tracked_dirty_sha256'].items())
                and all(sha(CONTROL / 'sources' / p) == h for p, h in control['source_sha256'].items())
                and all(sha(CONTROL / 'Release' / p) == h for p, h in control['release_sha256'].items()))

    require(unchanged(), 'Source, baseline or preserved control changed')
    require({p.relative_to(RELEASE).as_posix(): sha(p) for p in RELEASE.rglob('*') if p.is_file()}
            == control['release_sha256'], 'Current Release no longer equals preserved control')
    log = DATA / (args.prefix + '-build.log')
    receipt = DATA / (args.prefix + '-build.json')
    require(not log.exists() and not receipt.exists(), 'Existing build evidence must be reviewed')
    command = ['cmd.exe', '/d', '/c', str(COMMAND)]
    record = {'terminal': False, 'passed': False, 'status': 'building', 'command': command,
              'application_sha256': sha(application_path), 'source_count': len(sources),
              'source_sha256': sources, 'controller_sha256': sha(__file__),
              'build_command_source_sha256': COMMAND_SHA, 'started_at_unix': time.time()}
    def save():
        receipt.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
    save()
    child = None
    try:
        with log.open('xb') as stream:
            child = subprocess.Popen(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                                     stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
            record['pid'] = child.pid
            save()
            print('Existing Release build started:', child.pid, flush=True)
            record['exit_code'] = child.wait(timeout=1800)
        record['sources_unchanged'] = unchanged()
        record['passed'] = record['exit_code'] == 0 and record['sources_unchanged']
        record['status'] = 'build_passed' if record['passed'] else 'build_failed'
    except BaseException as error:
        record.update(status='build_failed_or_invalid', error=repr(error))
    finally:
        try:
            if child is not None and child.poll() is None:
                subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)],
                               capture_output=True, timeout=10, check=False)
                if child.poll() is None:
                    child.kill()
                child.wait(timeout=10)
            record['owned_child_cleanup_completed'] = child is not None and child.poll() is not None
        except BaseException as error:
            record.update(passed=False, status='build_failed_or_invalid', cleanup_error=repr(error))
        if not record.get('owned_child_cleanup_completed', False):
            record.update(passed=False, status='build_failed_or_invalid')
        record.update(terminal=True, finished_at_unix=time.time(),
                      release_sha256={p.relative_to(RELEASE).as_posix(): sha(p)
                                      for p in sorted(RELEASE.rglob('*')) if p.is_file()},
                      log=log.name, log_sha256=sha(log) if log.is_file() else None)
        save()
    print(json.dumps({'status': record['status'], 'receipt_sha256': sha(receipt)}, indent=2), flush=True)
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
