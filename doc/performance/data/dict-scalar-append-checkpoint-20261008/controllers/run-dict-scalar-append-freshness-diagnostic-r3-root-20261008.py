"""Run one unscored public DLL probe after checking the current R4 identity."""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path('D:/CantorAI/xlang3')
data = root / 'doc/performance/data'
prefix = 'dict-scalar-append-freshness-diagnostic-r3-20261008'
receipt = data / (prefix + '.json')
assert not any(data.glob(prefix + '*'))
assert sys.version_info[:3] == (3, 14, 7)
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
validation = data / 'frame-locals-retirement-r4-full-validation-20261008.json'
assert sha(validation) == 'b28809dcfc420906e3c3f656fdd9584140d0a80d778673a43342799ea64e75ec'
full = json.loads(validation.read_bytes())
paths = dict(full['source_sha256'], **full['binaries_sha256'],
             **{'build-repro/Release/' + p: h for p, h in full['baseline_sha256'].items()})
for path, expected in paths.items(): assert sha(root / path) == expected, path
raw = subprocess.check_output(['powershell', '-NoProfile', '-Command',
    'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'])
rows = json.loads(raw.decode('utf-8-sig') or '[]')
if isinstance(rows, dict): rows = [rows]
busy = [r for r in rows if r['ProcessId'] != os.getpid() and
        (r['Name'].lower().startswith(('python', 'xlang3')) or r['Name'].lower() in
         {'msbuild.exe', 'cl.exe', 'link.exe', 'ninja.exe', 'cmake.exe', 'ctest.exe'})]
assert not busy, busy
exe = root / 'scratch/performance/dict-scalar-append-freshness-diagnostic-r3-root-20261008.exe'
extras = [exe, exe.with_suffix('.cpp'), exe.with_suffix('.obj'),
          root / 'scratch/performance/build-dict-scalar-append-freshness-diagnostic-r3-root-20261008.cmd',
          data / 'dict-scalar-append-freshness-diagnostic-build-r3-20261008.log', validation]
extra_hashes = {str(p): sha(p) for p in extras}
environment = os.environ.copy()
environment['PATH'] = str(root / 'build-repro/main-verify-20261006/Release') + os.pathsep + environment['PATH']
record = {'status': 'preflight_verified', 'terminal': False, 'scored': False,
          'build_changes': False, 'engine_changes': False, 'source_count': 111,
          'release_count': 178, 'fixed_baseline_count': 177, 'busy': busy,
          'input_hashes': extra_hashes, 'started_utc': datetime.now(timezone.utc).isoformat()}
receipt.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
result = subprocess.run([str(exe)], cwd=root, env=environment, stdin=subprocess.DEVNULL,
                        capture_output=True, timeout=20, creationflags=subprocess.CREATE_NO_WINDOW)
stdout = data / (prefix + '.stdout.log')
stderr = data / (prefix + '.stderr.log')
stdout.write_bytes(result.stdout); stderr.write_bytes(result.stderr)
for path, expected in paths.items(): assert sha(root / path) == expected, path
assert all(sha(Path(p)) == expected for p, expected in extra_hashes.items())
record.update(terminal=True, exit_code=result.returncode, stdout_log=stdout.name,
              stdout_sha256=sha(stdout), stderr_log=stderr.name, stderr_sha256=sha(stderr),
              hashes_unchanged=True, completed_utc=datetime.now(timezone.utc).isoformat())
if result.returncode == 0 and result.stderr == b'':
    event = json.loads(result.stdout)
    assert event['status'] == 'completed_public_dll_index_state_diagnostic'
    assert event['append_count'] == 64 and event['fresh_before_append'] == 64
    assert event['all_values_correct'] and event['loaded_runtime_verified'] and not event['scored']
    record.update(status='terminal_completed_index_state_diagnostic', result=event)
else:
    record.update(status='terminal_failed_index_state_diagnostic')
receipt.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print(json.dumps(record, indent=2))
raise SystemExit(0 if result.returncode == 0 else 1)
