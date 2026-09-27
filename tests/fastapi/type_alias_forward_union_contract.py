from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class Payload(BaseModel):
    type Local = int
    value: "Local | Forward"


Forward = str
Payload.model_rebuild()


app = FastAPI()


@app.get("/alias-forward", response_model=Payload)
def alias_forward():
    return Payload(value="ready")


response = TestClient(app).get("/alias-forward")
print(response.status_code, response.json())
