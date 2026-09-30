from typing import Any, Callable

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ImportString


class Payload(BaseModel):
    callback: ImportString[Callable[[Any], Any]]


app = FastAPI()


@app.post("/import-callback")
def import_callback(payload: Payload):
    return {"name": payload.callback.__name__}


client = TestClient(app)
for callback in ("math.cos", "foobar", "os.missing"):
    response = client.post("/import-callback", json={"callback": callback})
    if response.status_code == 200:
        print(callback, response.status_code, response.json())
    else:
        print(callback, response.status_code,
              [(entry["type"], entry["msg"], entry.get("ctx"))
               for entry in response.json()["detail"]])
