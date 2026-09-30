"""Windows console signals reach a Python handler in a spawned process."""

import os
import signal
import subprocess
import sys
import time


if os.name != "nt":
    print("windows-only")
else:
    child_code = """
import signal
import time

received = False

def on_break(signum, frame):
    global received
    received = signum == signal.SIGBREAK
    print('handled', flush=True)

signal.signal(signal.SIGBREAK, on_break)
print('ready', flush=True)
deadline = time.monotonic() + 5
while not received and time.monotonic() < deadline:
    time.sleep(0.01)
if not received:
    raise RuntimeError('CTRL_BREAK handler did not run')
"""
    child = subprocess.Popen(
        [sys.executable, "-c", child_code],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
    )
    try:
        assert child.stdout is not None
        ready = child.stdout.readline().strip()
        assert ready == "ready", ready
        os.kill(child.pid, signal.CTRL_BREAK_EVENT)
        stdout, stderr = child.communicate(timeout=8)
        assert child.returncode == 0, (child.returncode, stdout, stderr)
        print(ready, stdout.strip(), child.returncode)
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
