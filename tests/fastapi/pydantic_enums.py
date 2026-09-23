from enum import Enum, IntEnum

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class Color(str, Enum):
    RED = "red"
    BLUE = "blue"


class Priority(IntEnum):
    LOW = 1
    HIGH = 2


class EnumPayload(BaseModel):
    color: Color
    priority: Priority


app = FastAPI()


@app.post("/enums", response_model=EnumPayload)
def enums(payload: EnumPayload) -> EnumPayload:
    return payload


with TestClient(app) as client:
    response = client.post("/enums", json={"color": "red", "priority": "2"})
    assert response.status_code == 200, response.text
    assert response.json() == {"color": "red", "priority": 2}

    response = client.post("/enums", json={"color": "green", "priority": 1})
    assert response.status_code == 422, response.text
    detail = response.json()["detail"][0]
    assert detail["type"] == "enum"
    assert detail["loc"] == ["body", "color"]

model = EnumPayload.model_validate({"color": "blue", "priority": 1})
assert model.color is Color.BLUE
assert model.priority is Priority.LOW
assert model.model_dump() == {"color": Color.BLUE, "priority": Priority.LOW}

print("fastapi-pydantic-enums-ok")
