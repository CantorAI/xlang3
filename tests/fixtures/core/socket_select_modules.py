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

receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
receiver.bind(("127.0.0.1", 0))
sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
print(sender.sendto(b"udp", receiver.getsockname()))
packet, address = receiver.recvfrom(16)
print(packet, address[0] == "127.0.0.1")
sender.sendto(b"xy", receiver.getsockname())
target = bytearray(4)
count, address = receiver.recvfrom_into(target)
print(count, bytes(target[:count]), address[0] == "127.0.0.1")
sender.close()
receiver.close()

print(signal.SIGBREAK, 21 in signal.valid_signals())
handler = lambda signum, frame: None
old = signal.signal(signal.SIGBREAK, handler)
print(signal.signal(signal.SIGBREAK, old) is handler)
for signum in (-1, 7):
    try:
        signal.signal(signum, handler)
    except ValueError:
        print(True)
