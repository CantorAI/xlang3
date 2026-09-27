from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class Payload[T](BaseModel):
    value: T


app = FastAPI()


@app.get("/generic-class", response_model=Payload[int])
def generic_class():
    return Payload[int](value="7")


response = TestClient(app).get("/generic-class")
print(response.status_code, response.json())
