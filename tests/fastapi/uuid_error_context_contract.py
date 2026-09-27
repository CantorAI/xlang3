from uuid import UUID

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class Payload(BaseModel):
    item: UUID


app = FastAPI()


@app.post("/uuid")
def uuid_endpoint(payload: Payload):
    return {"item": str(payload.item)}


client = TestClient(app)
for value in ("ebcdab58-6eb8-46fb-a190-d07a3", "", "invalid"):
    response = client.post("/uuid", json={"item": value})
    error = response.json()["detail"][0]
    print(response.status_code, error["type"], error["loc"], error["msg"], error["ctx"])
