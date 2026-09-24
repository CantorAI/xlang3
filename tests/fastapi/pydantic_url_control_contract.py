from fastapi import Body, FastAPI
from fastapi.responses import JSONResponse
from fastapi.testclient import TestClient
from pydantic_core import SchemaValidator, ValidationError, core_schema


validators = {
    "url": SchemaValidator(core_schema.url_schema()),
    "multi-host-url": SchemaValidator(core_schema.multi_host_url_schema()),
}

for name, validator in validators.items():
    assert str(validator.validate_python("\x00http://example.com")) == "http://example.com/"
    assert str(validator.validate_python("http://example.com/a\x00b")) == "http://example.com/a%00b"
    for value, expected in (
        ("\x14random\x00text", "relative URL without a base"),
        ("http://exa\x00mple.com", "invalid international domain name"),
    ):
        try:
            validator.validate_python(value)
        except ValidationError as exc:
            error = exc.errors(include_url=False)[0]
            assert error["type"] == "url_parsing"
            assert error["ctx"] == {"error": expected}
        else:
            raise AssertionError("invalid URL was accepted")
    print(name, "controls-ok")


app = FastAPI()


@app.post("/url/{kind}")
def parse_url(kind: str, value: str = Body(...)):
    try:
        return {"url": str(validators[kind].validate_python(value))}
    except ValidationError as exc:
        return JSONResponse(
            status_code=422,
            content={"error": exc.errors(include_url=False)[0]["ctx"]["error"]},
        )


with TestClient(app) as client:
    accepted = client.post("/url/multi-host-url", json="http://example.com/a\x00b")
    assert accepted.status_code == 200, accepted.text
    print("http", accepted.status_code, accepted.json())
    rejected = client.post("/url/url", json="http://exa\x00mple.com")
    assert rejected.status_code == 422, rejected.text
    print("http", rejected.status_code, rejected.json())
