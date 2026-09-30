from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class Payload(BaseModel):
    type Count = int
    count: Count


app = FastAPI()


@app.get("/alias-model", response_model=Payload)
def alias_model():
    return Payload(count=7)


response = TestClient(app).get("/alias-model")
print(response.status_code, response.json())
print(type(Payload.Count).__name__, Payload.Count.__value__ is int)
