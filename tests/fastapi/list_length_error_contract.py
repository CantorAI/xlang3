from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, Field


class Payload(BaseModel):
    values: list[int] = Field(min_length=2, max_length=4)


app = FastAPI()


@app.post("/list-length")
def list_length(payload: Payload):
    return payload.model_dump()


client = TestClient(app)
for values in ([1], [1, "x", "y"], [1, 2, "x", "y", 5]):
    response = client.post("/list-length", json={"values": values})
    errors = response.json()["detail"]
    print(values, response.status_code,
          [(entry["type"], entry["loc"]) for entry in errors])
