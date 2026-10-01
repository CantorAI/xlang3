"""Isolate native Windows overlapped completion waits from asyncio Task/Future.

Use the same loopback socket, overlapped receive, and producer thread in both
runtimes. This diagnostic includes thread wakeup/runtime costs and is not an
official pyperformance score or proof of where all TCP time is spent.
"""

import _overlapped
import socket
import statistics
import threading
import time


ITERATIONS = 200
receiver, sender = socket.socketpair()
ready = threading.Event()
errors = []
port = _overlapped.CreateIoCompletionPort(-1, 0, 0, 0)
_overlapped.CreateIoCompletionPort(receiver.fileno(), port, 0, 0)


def produce():
    try:
        for _ in range(ITERATIONS):
            if not ready.wait(5):
                raise AssertionError("producer did not receive request")
            ready.clear()
            sender.sendall(b"x")
    except BaseException as exc:
        errors.append(repr(exc))


thread = threading.Thread(target=produce)
thread.start()
samples = []
try:
    for _ in range(ITERATIONS):
        operation = _overlapped.Overlapped(0)
        operation.WSARecv(receiver.fileno(), 1, 0)
        started = time.perf_counter()
        ready.set()
        completion = _overlapped.GetQueuedCompletionStatus(port, 5000)
        elapsed = time.perf_counter() - started
        assert completion is not None, "receive completion timed out"
        error, size, key, address = completion
        assert (error, size, key, address) == (0, 1, 0, operation.address)
        assert operation.getresult() == b"x"
        samples.append(elapsed)
finally:
    ready.set()
    thread.join(timeout=5)
    receiver.close()
    sender.close()
    # CPython closes the OS completion-port handle; XLang3 has an internal
    # completion-port cleanup hook for its emulated handle registry.
    if hasattr(_overlapped, "_CloseIoCompletionPort"):
        _overlapped._CloseIoCompletionPort(port)
    else:
        import _winapi
        _winapi.CloseHandle(port)
assert not thread.is_alive(), "producer thread did not exit"
assert not errors, errors
print("native IOCP completion diagnostic", len(samples), "requests")
print("median_us", statistics.median(samples) * 1e6)
print("mean_us", statistics.mean(samples) * 1e6)
print("min_us", min(samples) * 1e6)
print("max_us", max(samples) * 1e6)
