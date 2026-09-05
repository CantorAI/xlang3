import os
import sys

endpoint = "lrpc:" + sys.argv[1]
import ipc_srv as srv thru endpoint
token = str(os.getpid())
payload = b"\x00xyz" * 262144
for i in range(12):
    reply = srv.pressure_echo(token, payload)
    assert reply[0] == i + 1, "remote operation was replayed"
    assert reply[1] == payload, "large response was corrupted"
    assert srv.make_bytes(1048576) == b"x" * 1048576, "large generated response was corrupted"
print("pressure-passed")
