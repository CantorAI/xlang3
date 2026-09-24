from typing import Any

from fastapi import Body, FastAPI
from fastapi.responses import JSONResponse
from fastapi.testclient import TestClient
from pydantic_core import SchemaValidator, ValidationError, core_schema


class Base:
    pass


class Child(Base):
    pass


validator = SchemaValidator(core_schema.is_subclass_schema(Base))
assert validator.validate_python(Child) is Child
for value in ("not-a-class", int):
    try:
        validator.validate_python(value)
    except ValidationError as error:
        detail = error.errors(include_url=False)[0]
        assert detail["type"] == "is_subclass_of"
        print("rejected", detail["type"])
    else:
        raise AssertionError("invalid subclass was accepted")


app = FastAPI()


@app.post("/subclass")
def check(payload: Any = Body(...)):
    try:
        validator.validate_python(payload)
    except ValidationError as error:
        return JSONResponse(
            status_code=422,
            content={"error": error.errors(include_url=False)[0]["type"]},
        )
    raise AssertionError("JSON data unexpectedly held a class")


with TestClient(app) as client:
    response = client.post("/subclass", json="not-a-class")
    assert response.status_code == 422, response.text
    assert response.json() == {"error": "is_subclass_of"}
    print("http", response.status_code, response.json())
