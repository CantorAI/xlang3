import json
import sys
from typing import Literal

from pydantic import BaseModel, Field, ValidationError


assert sys.implementation.name == "xlang3"


class AliasedPayload(BaseModel):
    schema_: dict[str, int] = Field(alias="schema", serialization_alias="schema")
    ref: str = Field(alias="$ref", serialization_alias="$ref")
    kind: Literal["local", "remote"]
    value: int | str
    note: str | None = None


class StrictPayload(BaseModel):
    count: int


integer_payload = AliasedPayload.model_validate(
    {"schema": {"count": 2}, "$ref": "item-1", "kind": "local", "value": 7}
)
assert integer_payload.schema_ == {"count": 2}
assert integer_payload.ref == "item-1"
assert integer_payload.value == 7
assert integer_payload.model_fields_set == {"schema_", "ref", "kind", "value"}
assert integer_payload.model_dump(by_alias=True, exclude_none=True) == {
    "schema": {"count": 2},
    "$ref": "item-1",
    "kind": "local",
    "value": 7,
}
assert json.loads(integer_payload.model_dump_json(by_alias=True, exclude_none=True)) == {
    "schema": {"count": 2},
    "$ref": "item-1",
    "kind": "local",
    "value": 7,
}

string_payload = AliasedPayload.model_validate(
    {"schema": {"count": 3}, "$ref": "item-2", "kind": "remote", "value": "seven"}
)
assert string_payload.value == "seven"

try:
    AliasedPayload.model_validate(
        {"schema": {"count": 1}, "$ref": "bad", "kind": "invalid", "value": 1}
    )
except ValidationError as error:
    detail = error.errors()[0]
    assert detail["type"] == "literal_error"
    assert detail["loc"] == ("kind",)
else:
    raise AssertionError("invalid literal must fail validation")

assert StrictPayload.model_validate({"count": "4"}).count == 4
try:
    StrictPayload.model_validate({"count": "4"}, strict=True)
except ValidationError as error:
    detail = error.errors()[0]
    assert detail["type"] == "int_type"
    assert detail["loc"] == ("count",)
else:
    raise AssertionError("strict validation must reject string integers")

print("pydantic-features-ok")
