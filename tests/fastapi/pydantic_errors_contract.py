from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, field_validator
from pydantic_core import PydanticCustomError, PydanticKnownError


custom = PydanticCustomError("not_ready", "Value {value} is not ready", {"value": 3})
known = PydanticKnownError("greater_than", {"gt": 0})
print(custom.type, custom.message_template, custom.context, custom.message(), repr(custom))
print(known.type, known.message_template, known.context, known.message(), repr(known))


class Payload(BaseModel):
    amount: int
    code: str

    @field_validator("amount")
    @classmethod
    def positive(cls, value: int) -> int:
        if value <= 0:
            raise PydanticKnownError("greater_than", {"gt": 0})
        return value

    @field_validator("code")
    @classmethod
    def allowed(cls, value: str) -> str:
        if value != "ok":
            raise PydanticCustomError("bad_code", "Code {code} is invalid", {"code": value})
        return value


app = FastAPI()


@app.post("/payload")
def create(payload: Payload) -> dict[str, object]:
    return payload.model_dump()


with TestClient(app) as client:
    accepted = client.post("/payload", json={"amount": 2, "code": "ok"})
    print(accepted.status_code, accepted.json())
    rejected = client.post("/payload", json={"amount": 0, "code": "no"})
    print(rejected.status_code, rejected.json()["detail"])
