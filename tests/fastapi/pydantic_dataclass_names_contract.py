from dataclasses import dataclass
from typing import Any

from fastapi import Body, FastAPI
from fastapi.responses import JSONResponse
from fastapi.testclient import TestClient
from pydantic_core import SchemaValidator, ValidationError, core_schema


@dataclass
class Child:
    value: str


@dataclass
class Parent:
    child: Child | None


validator = SchemaValidator(
    core_schema.dataclass_schema(
        Parent,
        core_schema.dataclass_args_schema(
            "Parent",
            [
                core_schema.dataclass_field(
                    "child",
                    core_schema.union_schema(
                        [
                            core_schema.dataclass_schema(
                                Child,
                                core_schema.dataclass_args_schema(
                                    "Child[args]",
                                    [core_schema.dataclass_field(
                                        "value", core_schema.str_schema()
                                    )],
                                ),
                                ["value"],
                                cls_name="Child[class]",
                            ),
                            core_schema.none_schema(),
                        ]
                    ),
                )
            ],
        ),
        ["child"],
    )
)


def error_details(value: Any):
    try:
        validator.validate_python(value)
    except ValidationError as error:
        return error.errors(include_url=False)
    raise AssertionError("invalid child was accepted")


details = error_details({"child": 123})
assert details[0]["loc"] == ("child", "Child[class]"), details
assert details[0]["ctx"] == {"class_name": "Child[args]"}, details
assert details[0]["msg"] == \
    "Input should be a dictionary or an instance of Child[args]", details
print("location", details[0]["loc"])


app = FastAPI()


@app.post("/dataclass")
def validate(payload: dict[str, Any] = Body(...)):
    try:
        validator.validate_python(payload)
    except ValidationError as error:
        first = error.errors(include_url=False)[0]
        return JSONResponse(status_code=422, content={
            "type": first["type"], "loc": first["loc"], "ctx": first["ctx"]
        })
    raise AssertionError("invalid request was accepted")


with TestClient(app) as client:
    response = client.post("/dataclass", json={"child": 123})
    assert response.status_code == 422, response.text
    assert response.json() == {
        "type": "dataclass_type",
        "loc": ["child", "Child[class]"],
        "ctx": {"class_name": "Child[args]"},
    }, response.text
    print("http", response.status_code, response.json())
