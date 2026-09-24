from dataclasses import dataclass
from datetime import date
from typing import Any

from fastapi import Body, FastAPI
from fastapi.responses import JSONResponse
from fastapi.testclient import TestClient
from pydantic_core import SchemaValidator, ValidationError, core_schema


bool_validator = SchemaValidator(core_schema.bool_schema())
int_validator = SchemaValidator(core_schema.int_schema())
float_validator = SchemaValidator(core_schema.float_schema())
date_validator = SchemaValidator(core_schema.date_schema())
fields_validator = SchemaValidator(
    core_schema.typed_dict_schema(
        {
            "enabled": core_schema.typed_dict_field(core_schema.bool_schema()),
            "count": core_schema.typed_dict_field(core_schema.int_schema()),
            "day": core_schema.typed_dict_field(core_schema.date_schema()),
        }
    )
)


@dataclass
class InputRecord:
    count: int
    day: date


dataclass_validator = SchemaValidator(
    core_schema.dataclass_schema(
        InputRecord,
        core_schema.dataclass_args_schema(
            "InputRecord",
            [
                core_schema.dataclass_field("count", core_schema.int_schema()),
                core_schema.dataclass_field("day", core_schema.date_schema()),
            ],
        ),
        ["count", "day"],
    )
)


assert bool_validator.validate_strings("true", strict=True) is True
assert bool_validator.validate_strings("false", strict=True) is False
assert int_validator.validate_strings("42", strict=True) == 42
assert float_validator.validate_strings("1.25", strict=True) == 1.25
assert date_validator.validate_strings("2026-09-23", strict=True) == date(2026, 9, 23)
assert dataclass_validator.validate_strings(
    {"count": "42", "day": "2026-09-23"}, strict=True
) == InputRecord(42, date(2026, 9, 23))
for validator, value, expected_type in (
    (int_validator, "invalid", "int_parsing"),
    (date_validator, "2026-09-23T01:00:00", "date_parsing"),
):
    try:
        validator.validate_strings(value, strict=True)
    except ValidationError as error:
        actual_type = error.errors(include_url=False)[0]["type"]
        assert actual_type == expected_type, actual_type
        print("rejected", actual_type)
    else:
        raise AssertionError("invalid string was accepted")


app = FastAPI()


@app.post("/validate-strings")
def validate_strings(payload: dict[str, Any] = Body(...)):
    try:
        validated = fields_validator.validate_strings(payload, strict=True)
    except ValidationError as error:
        return JSONResponse(
            status_code=422,
            content={"type": error.errors(include_url=False)[0]["type"]},
        )
    return {
        "enabled": validated["enabled"],
        "count": validated["count"],
        "day": validated["day"].isoformat(),
    }


@app.post("/validate-strings-dataclass")
def validate_strings_dataclass(payload: dict[str, Any] = Body(...)):
    record = dataclass_validator.validate_strings(payload, strict=True)
    return {"count": record.count, "day": record.day.isoformat()}


with TestClient(app) as client:
    response = client.post(
        "/validate-strings",
        json={"enabled": "true", "count": "42", "day": "2026-09-23"},
    )
    assert response.status_code == 200, response.text
    assert response.json() == {
        "enabled": True, "count": 42, "day": "2026-09-23"
    }
    print("http", response.status_code, response.json())

    response = client.post(
        "/validate-strings-dataclass",
        json={"count": "42", "day": "2026-09-23"},
    )
    assert response.status_code == 200, response.text
    assert response.json() == {"count": 42, "day": "2026-09-23"}
    print("dataclass", response.status_code, response.json())

    response = client.post(
        "/validate-strings",
        json={"enabled": "maybe", "count": "42", "day": "2026-09-23"},
    )
    assert response.status_code == 422, response.text
    assert response.json() == {"type": "bool_parsing"}
    print("http", response.status_code, response.json())
