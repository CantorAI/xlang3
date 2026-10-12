"""Build the applied generic GC repair in the existing Release directory."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
APP = DATA / 'gc-generic-cycles-applied-source-r3-20261009.json'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    assert sys.version_info[:3] == (3, 14, 7)
    assert sha(APP) == '165e474cac11bb2866e5c4534721f55ef15c287e09723f8c6b48c2b10d5fbeb0'
    application = json.loads(APP.read_text())
    assert all(sha(ROOT / p) == h for p, h in application['source_sha256'].items())
    command = ['cmd.exe', '/d', '/c', str(ROOT / 'scratch/performance/build-python-new-vm-continuation-r4-root-20261009.cmd')]
    log = DATA / 'gc-generic-cycles-build-r3-20261009.log'
    receipt = DATA / 'gc-generic-cycles-build-r3-20261009.json'
    assert not log.exists() and not receipt.exists()
    start = time.time()
    with log.open('xb') as stream:
        child = subprocess.Popen(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                                 stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
        print('Existing Release build started:', child.pid, flush=True)
        code = child.wait()
    unchanged = all(sha(ROOT / p) == h for p, h in application['source_sha256'].items())
    result = {'status': 'build_passed' if code == 0 and unchanged else 'build_failed',
        'terminal': True, 'passed': code == 0 and unchanged, 'exit_code': code,
        'command': command, 'build_command_source_sha256': sha(command[-1]), 'started_at_unix': start,
        'finished_at_unix': time.time(), 'application_sha256': sha(APP),
        'source_count': application['source_count'], 'source_sha256': application['source_sha256'],
        'sources_unchanged': unchanged, 'release_sha256': {p.relative_to(RELEASE).as_posix(): sha(p)
            for p in sorted(RELEASE.rglob('*')) if p.is_file()},
        'log': log.name, 'log_sha256': sha(log), 'controller_sha256': sha(__file__)}
    receipt.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(json.dumps({'status': result['status'], 'exit_code': code, 'sources_unchanged': unchanged,
                      'receipt_sha256': sha(receipt)}, indent=2), flush=True)
    return 0 if result['passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
