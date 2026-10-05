import os
import socket
import errno

assert hasattr(socket, 'SO_EXCLUSIVEADDRUSE') == (os.name == 'nt')
with socket.socket() as guard:
    if os.name == 'nt':
        guard.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
    guard.bind(('127.0.0.1', 0))
    address = guard.getsockname()
    with socket.socket() as duplicate:
        try:
            duplicate.bind(address)
        except OSError as exc:
            assert exc.errno == errno.EADDRINUSE
        else:
            raise AssertionError('duplicate socket acquired an exclusive address')
print('exclusive address ownership: ok')
