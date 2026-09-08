# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import pathlib
import socket
import subprocess
import sys
import time


EXPECTED = [
    "True", "200", "hello", "small", "True", "65536", "True", "4",
    "True", "True", "200", "hello", "True", "200",
]


def main():
    executable = sys.argv[1]
    modules = sys.argv[2] if len(sys.argv) > 2 else ""
    root = pathlib.Path(__file__).resolve().parent
    server = subprocess.Popen(
        [executable, str(root / "server.py"), modules],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if server.poll() is not None:
                stdout, stderr = server.communicate()
                raise RuntimeError(f"net server exited early: {stdout}\n{stderr}")
            try:
                with socket.create_connection(("127.0.0.1", 18173), timeout=0.1):
                    break
            except OSError:
                time.sleep(0.05)
        else:
            raise RuntimeError("net server did not open port 18173")

        result = subprocess.run(
            [executable, str(root / "client.py"), modules],
            text=True,
            capture_output=True,
            timeout=30,
        )
        actual = result.stdout.replace("\r\n", "\n").splitlines()
        if result.returncode or actual != EXPECTED:
            raise RuntimeError(
                f"net client failed ({result.returncode})\nexpected={EXPECTED!r}"
                f"\nactual={actual!r}\nstderr={result.stderr}"
            )
    finally:
        if server.poll() is None:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()


if __name__ == "__main__":
    main()
