import _overlapped
import _winapi
import threading
import time


port = _overlapped.CreateIoCompletionPort(
    _overlapped.INVALID_HANDLE_VALUE, 0, 0, 0
)
start = time.monotonic()
print(_overlapped.GetQueuedCompletionStatus(port, 25) is None)
print(time.monotonic() - start >= 0.020)


def complete():
    time.sleep(0.01)
    _overlapped.PostQueuedCompletionStatus(port, 7, 11, 12345)


worker = threading.Thread(target=complete)
worker.start()
print(_overlapped.GetQueuedCompletionStatus(port, 1000) == (0, 7, 11, 12345))
worker.join()
_winapi.CloseHandle(port)
