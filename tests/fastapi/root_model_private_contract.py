from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import RootModel


class Payload(RootModel[int]):
    _label: str = "initial"


app = FastAPI()


@app.get("/root-private")
def root_private():
    model = Payload(7)
    before = model._label
    model._label = "updated"
    return {"root": model.root, "before": before, "after": model._label}


response = TestClient(app).get("/root-private")
print(response.status_code, response.json())
