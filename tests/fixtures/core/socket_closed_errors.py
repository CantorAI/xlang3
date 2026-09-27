import errno
import socket

closed_code = getattr(errno, 'WSAENOTSOCK', errno.EBADF)
for operation in ('recv', 'send', 'connect'):
    sock = socket.socket()
    sock.close()
    try:
        if operation == 'recv':
            sock.recv(1)
        elif operation == 'send':
            sock.send(b'x')
        else:
            sock.connect(('127.0.0.1', 9))
    except OSError as error:
        print(operation, type(error).__name__, error.errno == closed_code, sock.fileno())
