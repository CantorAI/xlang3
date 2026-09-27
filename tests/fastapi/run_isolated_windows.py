"""Run a test command in its own hidden Windows console.

This keeps upstream worker-signal tests from signaling the invoking shell.
The child remains the requested interpreter; this helper only launches it.
"""

import os
import subprocess
import sys


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("usage: run_isolated_windows.py COMMAND [ARGS...]")
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        process = subprocess.Popen(
            sys.argv[1:],
            creationflags=subprocess.CREATE_NEW_CONSOLE,
            startupinfo=startup,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
    else:
        process = subprocess.Popen(sys.argv[1:])
    if process.stdout is not None:
        for chunk in iter(process.stdout.readline, b""):
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
    raise SystemExit(process.wait())
