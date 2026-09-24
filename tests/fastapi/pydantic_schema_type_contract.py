from typing import Any

from fastapi import Body, FastAPI
from fastapi.responses import JSONResponse
from fastapi.testclient import TestClient
from pydantic_core import PydanticKnownError, SchemaError, SchemaValidator


assert SchemaValidator({"type": "int"}).validate_python(4) == 4
try:
    SchemaValidator({"type": "bad"})
except SchemaError as error:
    assert 'Unknown schema type: "bad"' in str(error)
    print("schema", str(error))
else:
    raise AssertionError("unknown schema type was accepted")

try:
    PydanticKnownError("foobar")
except KeyError as error:
    assert str(error) == '"Invalid error type: \'foobar\'"'
    print("key", str(error))
else:
    raise AssertionError("unknown error type was accepted")


app = FastAPI()


@app.post("/schema")
def check(payload: Any = Body(...)):
    try:
        SchemaValidator(payload)
    except SchemaError as error:
        return JSONResponse(status_code=422, content={"error": str(error)})
    return {"accepted": True}


with TestClient(app) as client:
    response = client.post("/schema", json={"type": "bad"})
    assert response.status_code == 422, response.text
    assert 'Unknown schema type: "bad"' in response.json()["error"]
    print("http", response.status_code, response.json())
