"""Interpreter exit must drain native callbacks before deleting Python threads."""
import os
import queue
import secrets
import subprocess
import sys
import threading
import time


def child(extension, port, mode):
    sys.path.insert(0, extension)
    import xlang3

    builtins = xlang3.importModule("builtins")
    exit_requested = threading.Event()

    class Service:
        def ping(self):
            return 42

        def finish(self):
            # Main exits while this native worker still owns a Python callback.
            # The response must finish before listener/interpreter destruction.
            exit_requested.set()
            time.sleep(0.2)
            print("callback-finished", flush=True)
            return 73

    builtins.register_remote_object("shutdown_service", Service())
    builtins.lrpc_listen(port, False)
    print("ready", flush=True)
    if mode == "active":
        assert exit_requested.wait(15), "shutdown callback was not invoked"
    else:
        assert sys.stdin.readline().strip() == "exit"
    print("main-exiting", flush=True)
    # Deliberately retain module/listener globals through ordinary process exit.


def run(extension):
    sys.path.insert(0, extension)
    import xlang3

    for mode in ("idle", "active", "active", "idle"):
        port = 100000 + secrets.randbelow(1000000000)
        environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
        process = subprocess.Popen(
            [sys.executable, "-B", __file__, "--child", extension, str(port), mode],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, env=environment,
        )
        output = []
        lines = queue.Queue()

        def read_output():
            for line in process.stdout:
                output.append(line.rstrip())
                lines.put(line.rstrip())
            lines.put(None)

        reader = threading.Thread(target=read_output, daemon=True)
        reader.start()
        try:
            assert lines.get(timeout=15) == "ready", output
            service = xlang3.importModule("shutdown_service", thru=f"lrpc:{port}")
            assert service.ping() == 42
            if mode == "active":
                assert service.finish() == 73, output
            else:
                process.stdin.write("exit\n")
                process.stdin.flush()
            process.wait(timeout=15)
            reader.join(timeout=5)
            assert process.returncode == 0, (mode, process.returncode, output)
            assert "main-exiting" in output, output
            if mode == "active":
                assert "callback-finished" in output, output
            del service
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)
            reader.join(timeout=5)
            process.stdin.close()
            process.stdout.close()
    print("CPython bridge: idle and active native listener interpreter shutdown PASS")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--child":
        child(sys.argv[2], int(sys.argv[3]), sys.argv[4])
    else:
        run(sys.argv[1])
