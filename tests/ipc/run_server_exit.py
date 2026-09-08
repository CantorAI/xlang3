# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import pathlib
import random
import subprocess
import sys
import time


def main():
    executable = sys.argv[1]
    port = random.randrange(24001, 29000)
    root = pathlib.Path(__file__).resolve().parent
    server = subprocess.Popen(
        [executable, str(root / "server.py"), str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        ready = f"lrpc-ready:{port}"
        deadline = time.monotonic() + 10
        output = ""
        while time.monotonic() < deadline:
            if server.poll() is not None:
                stdout, stderr = server.communicate()
                raise RuntimeError(f"LRPC server exited before ready: {stdout}\n{stderr}")
            line = server.stdout.readline()
            output += line
            if ready in output:
                break
        else:
            raise RuntimeError(f"LRPC server did not become ready on port {port}")

        server.kill()
        server.wait()
        client = subprocess.run(
            [executable, str(root / "server_exit_client.py"), str(port)],
            text=True,
            capture_output=True,
            timeout=10,
        )
        if client.returncode == 0 or "ipc-smoke" in client.stdout:
            raise RuntimeError(
                "client unexpectedly read remote data after server exit: "
                f"{client.stdout}\n{client.stderr}"
            )
    finally:
        if server.poll() is None:
            server.kill()
            server.wait()


if __name__ == "__main__":
    main()
