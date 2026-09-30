import _socket
import socket


result = _socket.getaddrinfo(
    b"127.0.0.1", 80, _socket.AF_INET, _socket.SOCK_STREAM
)
print(result[0][4])
print(sorted((name, getattr(socket, name)) for name in dir(socket) if name.startswith("EAI_")))

try:
    _socket.getaddrinfo(bytearray(b"127.0.0.1"), 80)
except TypeError as exc:
    print(type(exc).__name__, str(exc))
