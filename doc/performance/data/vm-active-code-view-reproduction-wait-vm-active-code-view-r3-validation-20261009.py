"""Wait for foreign builds, then run the frozen validator once, without retries."""
import hashlib
import os
from pathlib import Path
import runpy
import subprocess
import sys
import time

ROOT = Path('D:/CantorAI/xlang3')
VALIDATOR = ROOT / 'scratch/performance/validate-vm-active-code-view-r3-proposed-20261009.py'
EXPECTED_SHA = 'a82be94f85b770b152faa25d23bc9059687da28b1f886a3d68bba289ad09c7f7'
assert sys.version_info[:3] == (3, 14, 7) and not sys.flags.optimize
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
query = "Get-CimInstance Win32_Process -Filter \"Name='cmake.exe' OR Name='cl.exe' OR Name='ctest.exe' OR Name='MSBuild.exe' OR Name='ninja.exe'\" | ForEach-Object { $_.Name + ':' + $_.ProcessId + ':' + $_.CreationDate.ToUniversalTime().ToString('o') }"
deadline = time.monotonic() + 600
previous = None
while True:
    result = subprocess.run(['C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe',
                             '-NoProfile', '-Command', query], capture_output=True,
                            creationflags=subprocess.CREATE_NO_WINDOW, timeout=20, check=True)
    assert not result.stderr, result.stderr
    busy = result.stdout.decode('ascii').strip()
    if busy != previous:
        print('Waiting for foreign build identities:' if busy else 'Build admission clear.', busy, flush=True)
        previous = busy
    if not busy:
        break
    if time.monotonic() >= deadline:
        raise SystemExit('Admission timeout; no validation or timing child launched.')
    time.sleep(5)
assert hashlib.sha256(VALIDATOR.read_bytes()).hexdigest() == EXPECTED_SHA
print('Starting frozen validator once in manager PID', os.getpid(), flush=True)
sys.argv = [str(VALIDATOR), '--application', str(ROOT / 'doc/performance/data/vm-active-code-view-r2-applied-source-20261009.json'),
            '--application-sha256', '4d05f0c03820cbbb77eab11b015a372c6479ed2471503f781f5c5900c8dcdb0b',
            '--build', str(ROOT / 'doc/performance/data/vm-active-code-view-r2-20261009-build.json'),
            '--build-sha256', '79ec9843abe376fe46c241c15fe5c6f3bdedc59696e6a410fe862d2531cd48e1',
            '--terminal-supplement', str(ROOT / 'doc/performance/data/pyperformance-xlang3-gc-r7b-per-definition-ownership-supplement-20261009-ledger.json'),
            '--terminal-supplement-sha256', '1b4722611ba85bcd570ac6223c5106c1551eda67accafb3cf0309211f1196128',
            '--prefix', 'vm-active-code-view-r3-validation-20261009']
runpy.run_path(str(VALIDATOR), run_name='__main__')
