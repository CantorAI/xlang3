from typing import Annotated, Literal, TypedDict

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, Field


class Metadata(TypedDict):
    count: int
    label: str


class Cat(BaseModel):
    kind: Literal["cat"]
    lives: int


class Dog(BaseModel):
    kind: Literal["dog"]
    good: bool


class StructuredPayload(BaseModel):
    metadata: Metadata
    pet: Annotated[Cat | Dog, Field(discriminator="kind")]


app = FastAPI()


@app.post("/structures", response_model=StructuredPayload)
def structures(payload: StructuredPayload) -> StructuredPayload:
    return payload


with TestClient(app) as client:
    response = client.post(
        "/structures",
        json={
            "metadata": {"count": "2", "label": "home"},
            "pet": {"kind": "cat", "lives": "9"},
        },
    )
    assert response.status_code == 200, response.text
    assert response.json() == {
        "metadata": {"count": 2, "label": "home"},
        "pet": {"kind": "cat", "lives": 9},
    }

    response = client.post(
        "/structures",
        json={
            "metadata": {"count": 1, "label": "park"},
            "pet": {"kind": "bird", "wings": 2},
        },
    )
    assert response.status_code == 422, response.text
    detail = response.json()["detail"][0]
    assert detail["type"] == "union_tag_invalid"
    assert detail["loc"] == ["body", "pet"]

    response = client.post(
        "/structures",
        json={"metadata": {"count": 1}, "pet": {"kind": "dog", "good": True}},
    )
    assert response.status_code == 422, response.text
    detail = response.json()["detail"][0]
    assert detail["type"] == "missing"
    assert detail["loc"] == ["body", "metadata", "label"]

print("fastapi-pydantic-structures-ok")
