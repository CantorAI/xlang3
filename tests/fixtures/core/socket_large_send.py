"""Exercise a single partial-capable send with a buffer larger than 64 KiB."""

import socket
import threading

left, right = socket.socketpair()
payload = bytes(range(256)) * 4096
received = bytearray()

def drain():
    while True:
        part = right.recv(65536)
        if not part:
            return
        received.extend(part)


reader = threading.Thread(target=drain)
reader.start()
try:
    sent = left.send(payload)
    left.shutdown(socket.SHUT_WR)
    reader.join()
    assert 0 < sent <= len(payload)
    assert bytes(received) == payload[:sent]
    print("large-send-ok", len(received) == sent)
finally:
    left.close()
    right.close()

left, right = socket.socketpair()
received = bytearray()
reader = threading.Thread(target=drain)
reader.start()
try:
    left.sendall(payload)
    left.shutdown(socket.SHUT_WR)
    reader.join()
    assert bytes(received) == payload
    print("large-sendall-ok", len(received) == len(payload))
finally:
    left.close()
    right.close()
