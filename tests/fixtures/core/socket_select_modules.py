import _socket
import socket
import select


print(socket.AF_INET)
print(socket.SOCK_STREAM)
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
print(s.family)
print(s.type)
print(s.fileno())
s.settimeout(1)
print(s.gettimeout())
print(select.select([], [], [], 0))
s.close()
print(_socket.AF_INET)
