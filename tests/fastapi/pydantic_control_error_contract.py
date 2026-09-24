import json

from fastapi import Body, FastAPI
from fastapi.responses import JSONResponse
from fastapi.testclient import TestClient
from pydantic_core import PydanticOmit, SchemaError, SchemaValidator, core_schema


def omit_value(value, _info):
    if value == "omit":
        raise PydanticOmit
    return value


validator = SchemaValidator(
    core_schema.with_info_plain_validator_function(omit_value)
)
assert validator.validate_python("keep") == "keep"
assert validator.validate_json('"keep"') == "keep"
for method, value in (
    (validator.validate_python, "omit"),
    (validator.validate_json, '"omit"'),
):
    try:
        method(value)
    except SchemaError as exc:
        assert "Uncaught Omit error" in str(exc)
        print("control", type(exc).__name__)
    else:
        raise AssertionError("PydanticOmit escaped without SchemaError")


app = FastAPI()


@app.post("/payload")
def create_payload(payload: str = Body(...)):
    try:
        return {"value": validator.validate_json(json.dumps(payload))}
    except SchemaError as exc:
        return JSONResponse(status_code=422, content={"error": type(exc).__name__})


with TestClient(app) as client:
    accepted = client.post("/payload", json="keep")
    assert accepted.status_code == 200, accepted.text
    print("http", accepted.status_code, accepted.json())
    rejected = client.post("/payload", json="omit")
    assert rejected.status_code == 422, rejected.text
    print("http", rejected.status_code, rejected.json())
