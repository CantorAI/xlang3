import os
import queue
import subprocess
import sys
import threading
import time

sys.path.insert(0, sys.argv[1])
import xlang3

runtime = xlang3.importModule("builtins")
endpoint = f"lrpc:{70000 + os.getpid()}"
start = time.monotonic()
assert runtime.lrpc_probe(endpoint, 60) is None
assert 0.04 <= time.monotonic() - start < 1.0
for invalid in ("http:1", "lrpc:0", "lrpc:../bad", "lrpc:"):
    try:
        runtime.lrpc_probe(invalid, 0)
    except RuntimeError:
        pass
    else:
        raise AssertionError(f"invalid endpoint accepted: {invalid}")

previous = None
for cycle in range(2):
    server = subprocess.Popen(
        [sys.argv[2], sys.argv[3], endpoint.split(":")[1]],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    lines = queue.Queue()
    def read_output():
        for line in server.stdout:
            lines.put(line.rstrip())
        lines.put(None)
    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    try:
        assert lines.get(timeout=15) == f"lrpc-ready:{endpoint.split(':')[1]}"
        state = runtime.lrpc_probe(endpoint, 1000)
        assert state is not None
        identity = (state["pid"], state["session_id"])
        assert identity[0] == server.pid and identity[1] > 0
        assert previous is None or previous != identity
        again = runtime.lrpc_probe(endpoint, 0)
        assert (again["pid"], again["session_id"]) == identity
        remote = xlang3.importModule("ipc_srv", thru=endpoint)
        assert remote.add(20, 22) == 42
        previous = identity
    finally:
        server.terminate()
        try:
            server.wait(timeout=10)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait(timeout=10)
        reader.join(timeout=5)
        server.stdout.close()
    assert runtime.lrpc_probe(endpoint, 0) is None

print("LRPC probe: bounded wait, validation, identity, exit, restart PASS")
