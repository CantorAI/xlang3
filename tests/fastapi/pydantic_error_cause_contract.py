from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ConfigDict, ValidationError, field_validator


class Payload(BaseModel):
    model_config = ConfigDict(validation_error_cause=True)

    count: int

    @field_validator("count")
    @classmethod
    def check_count(cls, value: int) -> int:
        if value < 0:
            try:
                raise AssertionError("negative")
            except AssertionError as original:
                raise ValueError("count is negative") from original
        return value


try:
    Payload.model_validate({"count": -1})
except ValidationError as error:
    group = error.__cause__
    assert isinstance(group, BaseExceptionGroup)
    assert len(group.exceptions) == 1
    cause = group.exceptions[0]
    assert repr(cause) == "ValueError('count is negative')"
    assert cause.__notes__[-1] == "\nPydantic: cause of loc: count"
    assert repr(cause.__cause__) == "AssertionError('negative')"
    assert cause.__cause__.__traceback__ is not None
    print(type(group).__name__, len(group.exceptions), cause.__notes__[-1].strip())
else:
    raise AssertionError("invalid payload was accepted")


app = FastAPI()


@app.post("/payload")
def create(payload: Payload) -> dict[str, int]:
    return payload.model_dump()


with TestClient(app) as client:
    accepted = client.post("/payload", json={"count": 2})
    assert accepted.status_code == 200
    print(accepted.status_code, accepted.json())

    rejected = client.post("/payload", json={"count": -1})
    assert rejected.status_code == 422
    detail = rejected.json()["detail"][0]
    print(rejected.status_code, detail["type"], detail["loc"], detail["msg"])
