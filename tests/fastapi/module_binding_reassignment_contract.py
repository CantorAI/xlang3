"""CPython oracle for calls through reassigned imported-module bindings."""

import json
import socket

from fastapi import FastAPI
from fastapi.testclient import TestClient


def current_socket_value():
    return socket.socket()


class SocketProxy:
    def socket(self):
        return "patched-socket"


real_socket = socket
socket = SocketProxy()
print("reassigned", current_socket_value())
socket = real_socket

app = FastAPI()


@app.get("/patched-module")
def patched_module():
    global socket
    socket = SocketProxy()
    try:
        return {"socket": current_socket_value()}
    finally:
        socket = real_socket


response = TestClient(app).get("/patched-module")
print("fastapi", response.status_code, json.dumps(response.json(), sort_keys=True))
