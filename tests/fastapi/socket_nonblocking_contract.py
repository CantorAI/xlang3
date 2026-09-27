import errno
import socket

from fastapi import FastAPI
from fastapi.testclient import TestClient

app = FastAPI()


@app.get('/socket-nonblocking')
def socket_nonblocking():
    would_block = getattr(errno, 'WSAEWOULDBLOCK', errno.EWOULDBLOCK)

    def result(operation):
        try:
            operation()
        except OSError as error:
            return [type(error).__name__, error.errno == would_block]
        return ['no-error', False]

    left, right = socket.socketpair()
    try:
        left.setblocking(False)
        right.setblocking(False)
        results = {
            'recv': result(lambda: left.recv(1)),
            'recv_into': result(lambda: left.recv_into(bytearray(1))),
            'recvfrom': result(lambda: left.recvfrom(1)),
            'recvfrom_into': result(lambda: left.recvfrom_into(bytearray(1))),
        }
        left.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 4096)
        payload = b'x' * 65536
        for _ in range(1000):
            try:
                left.send(payload)
            except OSError as error:
                results['send'] = [type(error).__name__, error.errno == would_block]
                break
        else:
            results['send'] = ['no-error', False]
    finally:
        left.close()
        right.close()

    closed_code = getattr(errno, 'WSAENOTSOCK', errno.EBADF)
    for method in ('recv', 'send', 'connect'):
        closed = socket.socket()
        closed.close()
        try:
            if method == 'recv':
                closed.recv(1)
            elif method == 'send':
                closed.send(b'x')
            else:
                closed.connect(('127.0.0.1', 9))
        except OSError as error:
            results['closed_' + method] = [
                type(error).__name__, error.errno == closed_code, closed.fileno()
            ]
    return results


response = TestClient(app).get('/socket-nonblocking')
print(response.status_code)
print(response.json())
