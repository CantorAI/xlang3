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

for label, host, flags, expected in (
    ('numeric-host-error', 'not-a-numeric-address', socket.AI_NUMERICHOST, 'EAI_NONAME'),
    ('bad-flags-error', '127.0.0.1', -1, 'EAI_BADFLAGS'),
):
    try:
        _socket.getaddrinfo(host, 80, _socket.AF_INET, _socket.SOCK_STREAM, 0, flags)
    except socket.gaierror as exc:
        print(label, exc.errno == getattr(socket, expected))
    else:
        print(label, False)

previous_timeout = socket.getdefaulttimeout()
try:
    socket.setdefaulttimeout(2)
    with socket.socket() as connection:
        print('default-timeout-type', type(socket.getdefaulttimeout()) is float,
              type(connection.gettimeout()) is float, connection.gettimeout() == 2.0)
        connection.settimeout(1)
        print('socket-timeout-type', type(connection.gettimeout()) is float,
              connection.gettimeout() == 1.0)
finally:
    socket.setdefaulttimeout(previous_timeout)
