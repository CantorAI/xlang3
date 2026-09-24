from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class RadixInput(BaseModel):
    digits: str
    base: int


app = FastAPI()


@app.post("/radix")
def parse_radix(value: RadixInput):
    return {"number": int(value.digits, base=value.base)}


with TestClient(app) as client:
    response = client.post("/radix", json={"digits": "ff", "base": 16})
    assert response.status_code == 200, response.text
    assert response.json() == {"number": 255}, response.text
    print(response.status_code, response.json())
