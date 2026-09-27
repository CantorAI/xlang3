import errno
import socket

would_block = getattr(errno, 'WSAEWOULDBLOCK', errno.EWOULDBLOCK)

def show(label, fn):
    try:
        fn()
    except OSError as error:
        print(label, type(error).__name__, error.errno == would_block)

left, right = socket.socketpair()
try:
    left.setblocking(False)
    right.setblocking(False)
    show('recv', lambda: left.recv(1))
    show('recv_into', lambda: left.recv_into(bytearray(1)))
    show('recvfrom', lambda: left.recvfrom(1))
    show('recvfrom_into', lambda: left.recvfrom_into(bytearray(1)))
    left.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 4096)
    data = b'x' * 65536
    for _ in range(1000):
        try:
            left.send(data)
        except OSError as error:
            print('send', type(error).__name__, error.errno == would_block)
            break
    else:
        print('send', 'not blocked')
finally:
    left.close()
    right.close()
