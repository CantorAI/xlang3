import _overlapped
import _winapi
import socket
import threading
import time


port = _overlapped.CreateIoCompletionPort(-1, 0, 0, 0)
receiver, sender = socket.socketpair()
_overlapped.CreateIoCompletionPort(receiver.fileno(), port, 37, 0)
operation = _overlapped.Overlapped(0)
operation.WSARecv(receiver.fileno(), 6, 0)
try:
    operation.getresult()
except OSError as exc:
    incomplete = exc.winerror == 996  # Windows ERROR_IO_INCOMPLETE
else:
    incomplete = False
print("pending-result", incomplete, operation.pending)


def send_later():
    time.sleep(0.005)
    sender.sendall(b"native")


thread = threading.Thread(target=send_later)
thread.start()
completion = _overlapped.GetQueuedCompletionStatus(port, 1000)
thread.join()
print("receive", completion == (0, 6, 37, operation.address),
      operation.getresult() == b"native", not operation.pending)

_overlapped.CreateIoCompletionPort(sender.fileno(), port, 91, 0)
send = _overlapped.Overlapped(0)
send.WSASend(sender.fileno(), b"send", 0)
completion = _overlapped.GetQueuedCompletionStatus(port, 1000)
print("send", completion == (0, 4, 91, send.address),
      send.getresult() == 4, receiver.recv(4) == b"send")

cancelled = _overlapped.Overlapped(0)
cancelled.WSARecv(receiver.fileno(), 1, 0)
cancelled.cancel()
completion = _overlapped.GetQueuedCompletionStatus(port, 1000)
try:
    cancelled.getresult()
except OSError as exc:
    aborted = exc.winerror == _overlapped.ERROR_OPERATION_ABORTED
else:
    aborted = False
print("cancel", completion == (_overlapped.ERROR_OPERATION_ABORTED, 0, 37,
                                cancelled.address), aborted, not cancelled.pending)


# Exercise repeated buffered receives, not only bytes-returning WSARecv.
# Stream transports reuse a bytearray and may receive queued or immediate
# completions; every packet count must agree with the published result/data.
bulk_receiver, bulk_sender = socket.socketpair()
_overlapped.CreateIoCompletionPort(bulk_receiver.fileno(), port, 101, 0)
payload = b"b" * (2 * 1024 * 1024)
thread = threading.Thread(target=bulk_sender.sendall, args=(payload,))
thread.start()
buffer = bytearray(32 * 1024)
received = 0
valid = True
while received < len(payload):
    receive = _overlapped.Overlapped(0)
    receive.WSARecvInto(bulk_receiver.fileno(), buffer, 0)
    completion = _overlapped.GetQueuedCompletionStatus(port, 1000)
    assert completion is not None
    count = receive.getresult()
    assert 0 < count <= len(buffer)
    assert completion == (0, count, 101, receive.address)
    valid = valid and bytes(buffer[:count]) == b"b" * count
    received += count
thread.join()
print("bulk-recv-into", received == len(payload), valid)
bulk_receiver.close()
bulk_sender.close()


# Use an explicit event and an unregistered socket for owner-drop cleanup.
# CPython requires IOCP users to retain the owner until its packet is drained;
# deleting a pending IOCP owner from the dequeueing thread can deadlock.
drop_event = _overlapped.CreateEvent(None, True, False, None)
drop_receiver, drop_sender = socket.socketpair()


def drop_pending_owner():
    orphan = _overlapped.Overlapped(drop_event)
    orphan.WSARecv(drop_receiver.fileno(), 1, 0)


drop_pending_owner()
try:
    _winapi.WaitForSingleObject(drop_event, 0)
except OSError as exc:
    closed_event = exc.winerror == 6
else:
    closed_event = False
print("dropped-owner", closed_event)
drop_receiver.close()
drop_sender.close()


def post_later():
    time.sleep(0.005)
    _overlapped.PostQueuedCompletionStatus(port, 3, 7, 12345)


thread = threading.Thread(target=post_later)
thread.start()
print("infinite-post", _overlapped.GetQueuedCompletionStatus(
    port, _overlapped.INFINITE) == (0, 3, 7, 12345))
thread.join()
started = time.monotonic()
print("timeout", _overlapped.GetQueuedCompletionStatus(port, 25) is None,
      time.monotonic() - started >= 0.020)

event = _overlapped.CreateEvent(None, True, False, None)


def signal_later():
    time.sleep(0.005)
    _overlapped.SetEvent(event)


thread = threading.Thread(target=signal_later)
thread.start()
print("native-wait", _winapi.WaitForSingleObject(event, 1000) == 0)
thread.join()
_overlapped.ResetEvent(event)
wait = _overlapped.RegisterWaitWithQueue(event, port, 56789, _overlapped.INFINITE)
thread = threading.Thread(target=signal_later)
thread.start()
completion = _overlapped.GetQueuedCompletionStatus(port, 1000)
thread.join()
_overlapped.UnregisterWaitEx(wait, -1)
print("registered-wait", completion == (0, 0, 0, 56789))
_winapi.CloseHandle(event)
receiver.close()
sender.close()
_winapi.CloseHandle(port)
print("closed", True)
