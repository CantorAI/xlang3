from typing import Any

from fastapi import Body, FastAPI
from fastapi.responses import JSONResponse
from fastapi.testclient import TestClient
from pydantic_core import SchemaValidator, ValidationError, core_schema


validator = SchemaValidator(
    core_schema.list_schema(items_schema=core_schema.int_schema())
)
assert validator.validate_json("[1,2]") == [1, 2]
assert validator.validate_json(b"[1,2]") == [1, 2]
assert validator.validate_json(bytearray(b"[1,2]")) == [1, 2]
print("accepted", [1, 2])

for value in ([], 3, None):
    try:
        validator.validate_json(value)
    except ValidationError as exc:
        error = exc.errors(include_url=False)[0]
        assert error["type"] == "json_type"
        assert error["msg"] == "JSON input should be string, bytes or bytearray"
        print("rejected", error["type"])
    else:
        raise AssertionError("invalid JSON input type was accepted")

for value, message in (
    ('"foobar', "Invalid JSON: EOF while parsing a string at line 1 column 7"),
    ("[1,\n2,\n3,]", "Invalid JSON: trailing comma at line 3 column 3"),
    ("", "Invalid JSON: EOF while parsing a value at line 1 column 0"),
):
    try:
        validator.validate_json(value)
    except ValidationError as exc:
        error = exc.errors(include_url=False)[0]
        assert error["type"] == "json_invalid"
        assert error["msg"] == message
        assert error["ctx"] == {"error": message.removeprefix("Invalid JSON: ")}
        print("syntax", error["msg"])
    else:
        raise AssertionError("malformed JSON was accepted")


app = FastAPI()


@app.post("/parse")
def parse(payload: Any = Body(...)):
    try:
        return {"numbers": validator.validate_json(payload)}
    except ValidationError as exc:
        error = exc.errors(include_url=False)[0]
        return JSONResponse(
            status_code=422,
            content={"error": error["type"], "message": error["msg"]},
        )


with TestClient(app) as client:
    accepted = client.post("/parse", json="[1,2]")
    assert accepted.status_code == 200, accepted.text
    print("http", accepted.status_code, accepted.json())
    rejected = client.post("/parse", json=[])
    assert rejected.status_code == 422, rejected.text
    print("http", rejected.status_code, rejected.json())
    malformed = client.post("/parse", json="[1,2,]")
    assert malformed.status_code == 422, malformed.text
    print("http", malformed.status_code, malformed.json())
