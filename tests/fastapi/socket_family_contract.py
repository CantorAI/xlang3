import socket

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/socket-family")
def socket_family():
    return {
        "ipx": int(socket.AF_IPX),
        "distinct_from_inet": socket.AF_IPX != socket.AF_INET,
    }


with TestClient(app) as client:
    response = client.get("/socket-family")
    assert response.status_code == 200, response.text
    assert response.json() == {"ipx": 6, "distinct_from_inet": True}, response.text
    print(response.status_code, response.json())
