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
print(isinstance(_socket.gethostname(), str), bool(_socket.gethostname()))
print(_socket.getprotobyname("tcp"))
try:
    _socket.htonl(-1)
except Exception as exc:
    print(type(exc).__name__)
host_info = _socket.gethostbyname_ex("localhost")
print(isinstance(host_info, tuple), host_info[1] == [], "127.0.0.1" in host_info[2])
print(_socket.inet_ntop(_socket.AF_INET6, _socket.inet_pton(_socket.AF_INET6, "::1")))
ipv6_rows = _socket.getaddrinfo("::1", 0, _socket.AF_INET6)
print(bool(ipv6_rows), ipv6_rows[0][0] == _socket.AF_INET6, len(ipv6_rows[0][4]))
print(_socket.getnameinfo(("::1", 0, 0, 0), _socket.NI_NUMERICHOST | _socket.NI_NUMERICSERV))

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

peek_left, peek_right = socket.socketpair()
peek_left.sendall(b"p")
print(peek_right.recv(1, _socket.MSG_PEEK), peek_right.recv(1))
peek_left.close()
peek_right.close()

into_left, into_right = socket.socketpair()
into_left.sendall(b"q")
into_buffer = bytearray(1)
print(into_right.recv_into(into_buffer, 1, _socket.MSG_PEEK), bytes(into_buffer), into_right.recv(1))
into_left.close()
into_right.close()

left, right = socket.socketpair()
left.sendall(b"close-write")
left.shutdown(socket.SHUT_WR)
print(right.recv(32), right.recv(1))
left.close()
right.close()

print(signal.SIGBREAK, 21 in signal.valid_signals())
handler = lambda signum, frame: None
old = signal.signal(signal.SIGBREAK, handler)
print(signal.signal(signal.SIGBREAK, old) is handler)
for signum in (-1, 7):
    try:
        signal.signal(signum, handler)
    except ValueError:
        print(True)
