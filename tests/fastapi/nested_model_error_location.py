from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, Field, TypeAdapter, ValidationError


class Item(BaseModel):
    value: int = Field(gt=0)


try:
    TypeAdapter(list[Item]).validate_python([{"value": -1}, {"value": -2}])
except ValidationError as error:
    print("adapter", [detail["loc"] for detail in error.errors()])


app = FastAPI()


@app.post("/items")
def save(items: list[Item]):
    return {"count": len(items)}


with TestClient(app) as client:
    response = client.post("/items", json=[{"value": -1}, {"value": -2}])
    print("route", response.status_code, [detail["loc"] for detail in response.json()["detail"]])
