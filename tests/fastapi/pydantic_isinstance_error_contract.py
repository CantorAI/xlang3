from fastapi import FastAPI
from fastapi.responses import JSONResponse
from fastapi.testclient import TestClient
from pydantic_core import SchemaValidator, core_schema


ordinary = SchemaValidator(core_schema.int_schema())
assert ordinary.isinstance_python(123) is True
assert ordinary.isinstance_python("not-an-int") is False
print("ordinary", True, False)

invalid_model = SchemaValidator(
    core_schema.model_schema(
        cls=int,
        schema=core_schema.model_fields_schema(
            fields={"field": core_schema.model_field(core_schema.int_schema())}
        ),
    )
)
for method, value in (
    (invalid_model.validate_python, {"field": 123}),
    (invalid_model.validate_json, '{"field":123}'),
    (invalid_model.isinstance_python, {"field": 123}),
):
    try:
        method(value)
    except AttributeError as exc:
        assert "__dict__" in str(exc)
        print("internal", type(exc).__name__)
    else:
        raise AssertionError("internal model error was swallowed")


app = FastAPI()


@app.get("/check")
def check():
    try:
        invalid_model.isinstance_python({"field": 123})
    except AttributeError as exc:
        return JSONResponse(status_code=422, content={"error": type(exc).__name__})
    raise AssertionError("internal model error was swallowed")


with TestClient(app) as client:
    response = client.get("/check")
    assert response.status_code == 422, response.text
    print("http", response.status_code, response.json())
