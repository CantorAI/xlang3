import socket
from unittest.mock import MagicMock


class StreamKind:
    def __index__(self):
        return socket.SOCK_STREAM


for kind in (StreamKind(), MagicMock()):
    results = socket.getaddrinfo("localhost", 0, type=kind)
    print(bool(results), all(item[1] == socket.SOCK_STREAM for item in results))
