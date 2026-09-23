from fastapi import FastAPI
from fastapi.testclient import TestClient
from typing import Annotated

from pydantic import (
    BaseModel,
    PlainValidator,
    ValidationInfo,
    WrapValidator,
    field_validator,
    model_validator,
)


def parse_code(value: object, info: ValidationInfo) -> int:
    assert info.field_name == "code"
    assert "count" in info.data
    bonus = 0 if info.context is None else info.context.get("bonus", 0)
    return int(value) + bonus


def wrap_score(value: object, handler: object, info: ValidationInfo) -> int:
    assert info.field_name == "score"
    if value == "default":
        return 41
    return handler(value) + 1


class ValidatedPayload(BaseModel):
    name: str
    count: int
    code: Annotated[int, PlainValidator(parse_code)]
    score: Annotated[int, WrapValidator(wrap_score)]

    @field_validator("name", mode="before")
    @classmethod
    def normalize_name(cls, value: str, info: ValidationInfo) -> str:
        assert info.field_name == "name"
        assert info.data == {}
        assert info.config["title"] == "ValidatedPayload"
        assert info.mode == "python"
        return value.strip().lower()

    @field_validator("count")
    @classmethod
    def double_count(cls, value: int, info: ValidationInfo) -> int:
        assert info.field_name == "count"
        assert info.data["name"] in {"alice", "bob", "carol"}
        if value > 10:
            raise ValueError("count is too large")
        bonus = 0 if info.context is None else info.context.get("bonus", 0)
        return value * 2 + bonus

    @model_validator(mode="after")
    def require_name(self, info: ValidationInfo) -> "ValidatedPayload":
        assert info.field_name is None
        if not self.name:
            raise ValueError("name is empty")
        return self


app = FastAPI()


@app.post("/validated", response_model=ValidatedPayload)
def validated(payload: ValidatedPayload) -> ValidatedPayload:
    return payload


with TestClient(app) as client:
    response = client.post(
        "/validated",
        json={"name": "  ALICE  ", "count": "4", "code": "7", "score": "9"},
    )
    assert response.status_code == 200, response.text
    assert response.json() == {"name": "alice", "count": 8, "code": 7, "score": 10}

    response = client.post(
        "/validated", json={"name": "Bob", "count": 11, "code": 1, "score": 2}
    )
    assert response.status_code == 422, response.text
    detail = response.json()["detail"][0]
    assert detail["type"] == "value_error"
    assert detail["loc"] == ["body", "count"]

    response = client.post(
        "/validated",
        json={"name": "Bob", "count": 2, "code": 1, "score": "bad"},
    )
    assert response.status_code == 422, response.text
    detail = response.json()["detail"][0]
    assert detail["type"] == "int_parsing"
    assert detail["loc"] == ["body", "score"]

model = ValidatedPayload.model_validate(
    {"name": " Carol ", "count": 4, "code": "5", "score": "default"},
    context={"bonus": 2},
)
assert model.name == "carol"
assert model.count == 10
assert model.code == 7
assert model.score == 41

print("fastapi-pydantic-validators-ok")
