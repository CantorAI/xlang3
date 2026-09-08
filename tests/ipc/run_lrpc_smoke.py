"""Run the existing XLang3 IPC fixtures with bounded process lifetimes."""
import argparse
from contextlib import ExitStack
from pathlib import Path
import secrets
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("--client", default="client.py")
    parser.add_argument("--expected", default="expected.out")
    parser.add_argument("--native-modules")
    parser.add_argument("--native-client")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    expected = (root / args.expected).read_text().rstrip()
    port = str(19000 + secrets.randbelow(5000))
    processes = []
    with tempfile.TemporaryDirectory(prefix="xlang3-ipc-") as directory, ExitStack() as stack:
        directory = Path(directory)

        def start(name, command):
            output = directory / (name + ".out")
            errors = directory / (name + ".err")
            process = subprocess.Popen(
                command, stdout=stack.enter_context(output.open("w")),
                stderr=stack.enter_context(errors.open("w")))
            processes.append(process)
            return process, output, errors

        def check(client):
            process, output, errors = client
            process.wait(timeout=60)
            actual = output.read_text().rstrip()
            if process.returncode != 0 or actual != expected:
                raise RuntimeError(f"Client exit={process.returncode}\n{actual}\n{errors.read_text()}")

        try:
            command = [args.executable, str(root / "server.py"), port]
            if args.native_modules:
                command.append(args.native_modules)
            server, output, errors = start("server", command)
            deadline = time.monotonic() + 15
            while f"lrpc-ready:{port}" not in output.read_text():
                if server.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError(f"Server not ready:\n{output.read_text()}\n{errors.read_text()}")
                time.sleep(0.05)
            command = [args.executable, str(root / args.client), port]
            check(start("serial", command))
            clients = [start(f"parallel-{i}", command) for i in range(4)]
            for client in clients:
                check(client)
            if args.native_client:
                native = subprocess.run(
                    [args.native_client, port], text=True, capture_output=True, timeout=30)
                if native.returncode:
                    raise RuntimeError(
                        f"Native client exit={native.returncode}\n{native.stdout}\n{native.stderr}")
            if server.poll() is not None:
                raise RuntimeError(f"Server exited unexpectedly: {errors.read_text()}")
            print(f"IPC {args.client}: serial and four parallel clients passed")
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
