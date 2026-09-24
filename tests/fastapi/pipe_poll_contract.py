from multiprocessing import Pipe

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/pipe-poll")
def pipe_poll():
    parent, child = Pipe()
    try:
        empty = parent.poll(0.02)
        child.send_bytes(b"ready")
        available = parent.poll(0.02)
        payload = parent.recv_bytes().decode("ascii")
        return {"empty": empty, "available": available, "payload": payload}
    finally:
        parent.close()
        child.close()


with TestClient(app) as client:
    response = client.get("/pipe-poll")
    assert response.status_code == 200
    assert response.json() == {"empty": False, "available": True, "payload": "ready"}
    print(response.json())
