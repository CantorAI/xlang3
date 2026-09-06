import sys
import time

class Box:
    def __init__(self, value):
        self._value = value

    def value(self):
        return self._value

    @property
    def current(self):
        return self._value

class Service:
    Box = Box

    def add(self, left, right):
        return left + right

    def echo(self, value):
        return "echo:" + value

    def bytes_echo(self, value):
        return value

    def invoke_python(self, callback, value):
        return callback(value, offset=3)

port = int(sys.argv[1])
register_remote_object("ipc_srv", Service())
lrpc_listen(port, False)
sys.stdout.write("lrpc-ready:" + str(port) + "\n")
sys.stdout.flush()
while True:
    time.sleep(1)
