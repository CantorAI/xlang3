import warnings
from typing import Any

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ConfigDict, Field, ValidationError, root_validator


with warnings.catch_warnings():
    warnings.simplefilter("ignore", DeprecationWarning)

    class Payload(BaseModel):
        current_value: float = Field(alias="current")
        max_value: float
        model_config = ConfigDict(validate_assignment=True)

        @root_validator(pre=True)
        def reject_strings(cls, values: dict[str, Any]) -> dict[str, Any]:
            if any(isinstance(value, str) for value in values.values()):
                raise ValueError("values cannot be a string")
            return values


app = FastAPI()


@app.get("/before-assignment")
def before_assignment():
    item = Payload(current=100, max_value=200)
    try:
        item.current_value = "100"
    except ValidationError as error:
        first = error.errors(include_url=False)[0]
        failure = {
            "type": first["type"],
            "loc": first["loc"],
            "input": first["input"],
        }
    item.current_value = 125
    return {"failure": failure, "current": item.current_value}


response = TestClient(app).get("/before-assignment")
print(response.status_code, response.json())
