import socket


print(hasattr(socket, "AF_IPX"))
print(socket.AF_IPX == 6)
print(socket.AF_IPX != socket.AF_INET)
