import os
import gc
import queue
import subprocess
import sys
import threading
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, sys.argv[1])
import xlang3

port = 40000 + os.getpid() % 20000
server = subprocess.Popen(
    [sys.argv[2], sys.argv[3], str(port)],
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
    line = lines.get(timeout=15)
    assert line == f"lrpc-ready:{port}", line
    remote = xlang3.importModule("ipc_srv", thru=f"lrpc:{port}")
    assert remote.add(20, 22) == 42
    assert remote.echo("CPython") == "echo:CPython"
    assert remote.invoke_python(lambda value, offset: remote.add(value, offset), 39) == 42
    payload = bytes(range(256)) * 4097
    assert remote.bytes_echo(payload) == payload
    try:
        remote.bytes_echo(bytes(range(256)) * 8193)
    except RuntimeError as error:
        assert "stream write failed" in str(error), str(error)
    else:
        raise AssertionError("expected rejection above the existing IPC session capacity")
    assert remote.add(1, 2) == 3, "oversized request must not break the session"
    box = remote.Box(73)
    assert box.value() == 73
    assert box.current == 73
    with ThreadPoolExecutor(max_workers=4) as pool:
        assert list(pool.map(lambda i: remote.add(i, 1), range(24))) == list(range(1, 25))
        sizes = (0, 1, 65535, 65536, 65537, 262144, 524288, 1048576)

        def echo_binary(index):
            size = sizes[index % len(sizes)]
            data = bytes([index % 256]) * size
            assert remote.bytes_echo(data) == data, (index, size)
            return size

        assert list(pool.map(echo_binary, range(24))) == list(sizes) * 3
    del box, remote
    del sys.modules["xlang3"]
    del xlang3
    gc.collect()
    import xlang3
    remote = xlang3.importModule("ipc_srv", thru=f"lrpc:{port}")
    assert remote.invoke_python(lambda value, offset: remote.add(value, offset), 39) == 42
    print("CPython -> XLang3 shared-memory IPC: calls, large binary, returned objects, threaded callers PASS")
finally:
    server.terminate()
    try:
        server.wait(timeout=10)
    except subprocess.TimeoutExpired:
        server.kill()
        server.wait(timeout=10)
    reader.join(timeout=5)
    server.stdout.close()
