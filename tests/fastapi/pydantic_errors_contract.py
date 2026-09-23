from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, field_validator
from pydantic_core import PydanticCustomError, PydanticKnownError, ValidationError


custom = PydanticCustomError("not_ready", "Value {value} is not ready", {"value": 3})
known = PydanticKnownError("greater_than", {"gt": 0})
print(custom.type, custom.message_template, custom.context, custom.message(), repr(custom))
print(known.type, known.message_template, known.context, known.message(), repr(known))


class CustomErrorWithCustomTemplate(PydanticCustomError):
    def __new__(cls, error_type: str, setting: str, context: dict[str, str]):
        template = "Setting {setting} rejects {wrong_value}"
        return super().__new__(cls, error_type, template,
                               {**context, "setting": setting})


subclass_error = CustomErrorWithCustomTemplate(
    "bad_setting", "strict", {"wrong_value": "no"})
print(subclass_error.type, subclass_error.message(), subclass_error.context)


class LocationError(ValidationError):
    def errors(self, *, include_url=True, include_context=True, include_input=True):
        items = super().errors(include_url=include_url,
                               include_context=include_context,
                               include_input=include_input)
        return [{**item, "loc": item["loc"][1:]} for item in items]


location_error = LocationError.from_exception_data(
    "LocationError", [{"type": "value_error", "loc": ("hidden", "code"),
                       "msg": "supplied message is ignored", "input": "no",
                       "ctx": {"error": "bad value"}}])
normalized = location_error.errors(include_url=False)[0]
print(normalized["type"], normalized["loc"], normalized["msg"])


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


class SubclassPayload(BaseModel):
    setting: str

    @field_validator("setting")
    @classmethod
    def allowed_setting(cls, value: str) -> str:
        if value != "strict":
            raise CustomErrorWithCustomTemplate(
                "bad_setting", "strict", {"wrong_value": value})
        return value

app = FastAPI()


@app.post("/payload")
def create(payload: Payload) -> dict[str, object]:
    return payload.model_dump()


@app.post("/subclass")
def create_subclass(payload: SubclassPayload) -> dict[str, str]:
    return payload.model_dump()


with TestClient(app) as client:
    accepted = client.post("/payload", json={"amount": 2, "code": "ok"})
    print(accepted.status_code, accepted.json())
    rejected = client.post("/payload", json={"amount": 0, "code": "no"})
    print(rejected.status_code, rejected.json()["detail"])
    subclass_rejected = client.post("/subclass", json={"setting": "no"})
    subclass_detail = subclass_rejected.json()["detail"][0]
    print(subclass_rejected.status_code, subclass_detail["type"],
          subclass_detail["msg"])
