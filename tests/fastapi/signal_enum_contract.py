import os
import signal

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/signal-enum")
def signal_enum():
    try:
        os.kill(99999999, signal.SIGTERM)
    except OSError:
        return {"integer_subclass_accepted": True}
    return {"integer_subclass_accepted": False}


with TestClient(app) as client:
    response = client.get("/signal-enum")
    assert response.status_code == 200
    assert response.json() == {"integer_subclass_accepted": True}
    print(response.json())
