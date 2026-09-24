import _winapi
from multiprocessing import Pipe


parent, child = Pipe()
print(_winapi.PeekNamedPipe(parent.fileno()))
print(_winapi.PeekNamedPipe(parent.fileno(), 1))
print(parent.poll(0.02))
child.send_bytes(b"abc")
print(_winapi.PeekNamedPipe(parent.fileno()))
print(_winapi.PeekNamedPipe(parent.fileno(), 1))
print(parent.poll(0.02), parent.recv_bytes())
parent.close()
child.close()
