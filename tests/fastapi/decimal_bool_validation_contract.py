from decimal import Decimal

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ValidationError


class Model(BaseModel):
    value: bool


app = FastAPI()


@app.get("/decimal-bool")
def decimal_bool(value: str):
    try:
        return {"value": Model(value=Decimal(value)).value}
    except ValidationError as error:
        return {"errors": [entry["type"] for entry in error.errors(include_url=False)]}


@app.post("/bytes-bool")
def bytes_bool(value: list[int]):
    try:
        return {"value": Model(value=bytes(value)).value}
    except ValidationError as error:
        return {"errors": [entry["type"] for entry in error.errors(include_url=False)]}


client = TestClient(app)
for value in ("0", "1", "2", "0.5", "NaN", "Infinity"):
    response = client.get("/decimal-bool", params={"value": value})
    print(value, response.status_code, response.json())
for value in (b"TRUE", b"FALSE", b"2", b"\x81"):
    response = client.post("/bytes-bool", json=list(value))
    print(repr(value), response.status_code, response.json())
