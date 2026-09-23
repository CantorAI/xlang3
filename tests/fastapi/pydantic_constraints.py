from typing import Annotated

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, Field, TypeAdapter, ValidationError, field_validator
from pydantic_core import PydanticUndefined, SchemaValidator


class Measurements(BaseModel):
    count: Annotated[int, Field(gt=0, lt=10, multiple_of=2)]
    ratio: Annotated[float, Field(ge=0.5, le=2.0, multiple_of=0.25)]
    values: Annotated[list[int], Field(min_length=2, max_length=3)]
    labels: Annotated[dict[str, int], Field(min_length=1, max_length=2)]


app = FastAPI()


@app.post("/measurements", response_model=Measurements)
def measurements(value: Measurements) -> Measurements:
    return value


with TestClient(app) as client:
    response = client.post(
        "/measurements",
        json={"count": "4", "ratio": "1.25", "values": [1, "2"], "labels": {"a": "3"}},
    )
    assert response.status_code == 200, response.text
    assert response.json() == {
        "count": 4,
        "ratio": 1.25,
        "values": [1, 2],
        "labels": {"a": 3},
    }

    response = client.post(
        "/measurements",
        json={"count": 0, "ratio": 1.0, "values": [1, 2], "labels": {"a": 1}},
    )
    assert response.status_code == 422, response.text
    assert response.json()["detail"][0]["type"] == "greater_than"

    response = client.post(
        "/measurements",
        json={"count": 4, "ratio": 2.5, "values": [1, 2], "labels": {"a": 1}},
    )
    assert response.status_code == 422, response.text
    assert response.json()["detail"][0]["type"] == "less_than_equal"

    response = client.post(
        "/measurements",
        json={"count": 4, "ratio": 1.0, "values": [], "labels": {"a": 1}},
    )
    assert response.status_code == 422, response.text
    assert response.json()["detail"][0]["type"] == "too_short"

try:
    Measurements.model_validate(
        {"count": 3, "ratio": 1.0, "values": [1, 2], "labels": {"a": 1}}
    )
except ValidationError as error:
    detail = error.errors()[0]
    assert detail["type"] == "multiple_of"
    assert detail["loc"] == ("count",)
else:
    raise AssertionError("integer multiple_of must be enforced")

try:
    Measurements.model_validate(
        {"count": 4, "ratio": "1.25", "values": [1, 2], "labels": {"a": 1}},
        strict=True,
    )
except ValidationError as error:
    detail = error.errors()[0]
    assert detail["type"] == "float_type"
    assert detail["loc"] == ("ratio",)
else:
    raise AssertionError("strict float validation must reject strings")

try:
    Measurements.model_validate(
        {"count": 4, "ratio": 1.0, "values": [1, "bad"], "labels": {"a": 1}}
    )
except ValidationError as error:
    detail = error.errors()[0]
    assert detail["type"] == "int_parsing"
    assert detail["loc"] == ("values", 1)
else:
    raise AssertionError("list item errors must preserve their index")

try:
    Measurements.model_validate(
        {"count": 4, "ratio": 1.0, "values": [1, 2], "labels": {"a": "bad"}}
    )
except ValidationError as error:
    detail = error.errors()[0]
    assert detail["type"] == "int_parsing"
    assert detail["loc"] == ("labels", "a")
else:
    raise AssertionError("dictionary value errors must preserve their key")

finite_adapter = TypeAdapter(Annotated[float, Field(allow_inf_nan=False)])
for non_finite in (float("inf"), float("-inf"), float("nan")):
    try:
        finite_adapter.validate_python(non_finite)
    except ValidationError as error:
        detail = error.errors()[0]
        assert detail["type"] == "finite_number"
        assert detail["msg"] == "Input should be a finite number"
    else:
        raise AssertionError("allow_inf_nan=False must reject non-finite values")

short_string_adapter = TypeAdapter(Annotated[str, Field(min_length=1)])
try:
    short_string_adapter.validate_python("")
except ValidationError as error:
    detail = error.errors()[0]
    assert detail["type"] == "string_too_short"
    assert detail["msg"] == "String should have at least 1 character"
else:
    raise AssertionError("min_length=1 must reject an empty string")

set_adapter = TypeAdapter(Annotated[set[str], Field(min_length=2, max_length=3)])
assert set_adapter.validate_python(["a", "a", "b", "c"]) == {"a", "b", "c"}
for invalid, expected_type, expected_message, expected_actual in (
    (["a"], "too_short", "Set should have at least 2 items after validation, not 1", 1),
    (["a", "b", "c", "d"], "too_long", "Set should have at most 3 items after validation, not more", None),
):
    try:
        set_adapter.validate_python(invalid)
    except ValidationError as error:
        detail = error.errors()[0]
        assert detail["type"] == expected_type
        assert detail["msg"] == expected_message
        assert detail["ctx"]["field_type"] == "Set"
        assert detail["ctx"]["actual_length"] is expected_actual
    else:
        raise AssertionError("set length constraints must use validated cardinality")

assert repr(PydanticUndefined) == "PydanticUndefined"

bool_adapter = TypeAdapter(bool)
for value, expected in (("True", True), ("true", True), ("1", True),
                        ("False", False), ("false", False), ("0", False)):
    assert bool_adapter.validate_python(value) is expected
try:
    bool_adapter.validate_python("not-a-bool")
except ValidationError as error:
    detail = error.errors()[0]
    assert detail["type"] == "bool_parsing"
    assert detail["msg"] == "Input should be a valid boolean, unable to interpret input"
else:
    raise AssertionError("invalid boolean strings must fail with bool_parsing")

union_validator = SchemaValidator(
    {
        "type": "union",
        "choices": [
            {"type": "literal", "expected": ["array"]},
            {"type": "list", "items_schema": {"type": "str"}},
        ],
    },
    {"title": "Schema"},
)
try:
    union_validator.validate_python(True)
except ValidationError as error:
    assert error.title == "Schema"
    assert len(error.errors()) == 2
    assert str(error).startswith("2 validation errors for Schema")
else:
    raise AssertionError("union validation must preserve every branch error")


class NestedChild(BaseModel):
    visible: str
    hidden: str


class NestedParent(BaseModel):
    name: str
    child: NestedChild


nested = NestedParent(
    name="parent", child=NestedChild(visible="yes", hidden="no")
)
assert nested.model_dump(exclude={"child": {"hidden"}}) == {
    "name": "parent",
    "child": {"visible": "yes"},
}

callable_discriminator = SchemaValidator(
    {
        "type": "tagged-union",
        "discriminator": lambda value: value["kind"],
        "choices": {"cat": {"type": "any"}, "dog": {"type": "any"}},
    }
)
tagged_value = {"kind": "dog", "barks": 3.5}
assert callable_discriminator.validate_python(tagged_value) == tagged_value


class ValidatedName(BaseModel):
    name: str

    @field_validator("name")
    def require_suffix(cls, value: str) -> str:
        if not value.endswith("A"):
            raise ValueError("name must end in A")
        return value


try:
    ValidatedName(name="modelX")
except ValidationError as error:
    detail = error.errors()[0]
    assert detail["type"] == "value_error"
    assert detail["msg"] == "Value error, name must end in A"
    assert detail["input"] == "modelX"
    assert isinstance(detail["ctx"]["error"], ValueError)
    assert repr(detail["ctx"]["error"]) == "ValueError('name must end in A')"
else:
    raise AssertionError("field validator ValueError must become a validation error")

print("fastapi-pydantic-constraints-ok")
