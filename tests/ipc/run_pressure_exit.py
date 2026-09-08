# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import pathlib
import random
import subprocess
import sys
import tempfile
import time
import uuid


def wait_for(path, process, message):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if path.exists():
            return
        if process.poll() is not None:
            raise RuntimeError(message)
        time.sleep(0.05)
    raise RuntimeError(message)


def main():
    executable = sys.argv[1]
    root = pathlib.Path(__file__).resolve().parent
    port = random.randrange(30001, 35000)
    prefix = pathlib.Path(tempfile.gettempdir()) / f"xlang3-pressure-exit-{uuid.uuid4().hex}"
    server = subprocess.Popen(
        [executable, str(root / "pressure_exit_server.py"), str(port), str(prefix)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    clients = []
    try:
        wait_for(pathlib.Path(f"{prefix}.ready"), server, "pressure server failed to start")
        for _ in range(2):
            clients.append(subprocess.Popen(
                [executable, str(root / "pressure_exit_client.py"), str(port)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            ))
        wait_for(pathlib.Path(f"{prefix}.entered"), server, "large call did not enter server")
        time.sleep(0.5)
        if any(client.poll() is not None for client in clients):
            raise RuntimeError("capacity wait failed before the server was stopped")
        server.kill()
        server.wait()
        for client in clients:
            stdout, stderr = client.communicate(timeout=10)
            if client.returncode == 0 or "exited" not in stderr:
                raise RuntimeError(f"unexpected pressure-exit result: {stdout}\n{stderr}")
    finally:
        for client in clients:
            if client.poll() is None:
                client.kill()
                client.wait()
        if server.poll() is None:
            server.kill()
            server.wait()
        for suffix in (".ready", ".entered"):
            pathlib.Path(f"{prefix}{suffix}").unlink(missing_ok=True)


if __name__ == "__main__":
    main()
