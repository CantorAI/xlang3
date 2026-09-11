import _socket
import socket
import select
import signal


print(socket.AF_INET)
print(socket.SOCK_STREAM)
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
print(s.family)
print(s.type)
print(s.fileno() >= 0)
s.settimeout(1)
print(s.gettimeout())
print(select.select([], [], [], 0))
s.close()
print(_socket.AF_INET)
print(signal.SIGBREAK, 21 in signal.valid_signals())
handler = lambda signum, frame: None
old = signal.signal(signal.SIGBREAK, handler)
print(signal.signal(signal.SIGBREAK, old) is handler)
for signum in (-1, 7):
    try:
        signal.signal(signum, handler)
    except ValueError:
        print(True)
