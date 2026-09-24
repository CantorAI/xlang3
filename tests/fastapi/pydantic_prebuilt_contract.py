from typing import Any

from fastapi import Body, FastAPI
from fastapi.testclient import TestClient
from pydantic_core import SchemaSerializer, SchemaValidator, core_schema


class Inner:
    x: int


class Outer:
    inner: Inner


inner_schema = core_schema.model_schema(
    Inner,
    core_schema.model_fields_schema(
        {"x": core_schema.model_field(core_schema.int_schema())}
    ),
)
Inner.__pydantic_complete__ = True
Inner.__pydantic_validator__ = SchemaValidator(inner_schema)
Inner.__pydantic_serializer__ = SchemaSerializer(inner_schema)

outer_schema = core_schema.model_schema(
    Outer,
    core_schema.model_fields_schema(
        {
            "inner": core_schema.model_field(
                core_schema.model_schema(
                    Inner,
                    core_schema.model_fields_schema(
                        {"x": core_schema.model_field(core_schema.str_schema())}
                    ),
                )
            )
        }
    ),
)
validator = SchemaValidator(outer_schema)
serializer = SchemaSerializer(outer_schema)
assert "PrebuiltValidator" in repr(validator)
assert "PrebuiltSerializer" in repr(serializer)
value = validator.validate_python({"inner": {"x": 7}})
assert value.inner.x == 7
assert serializer.to_python(value) == {"inner": {"x": 7}}
print("prebuilt", value.inner.x, serializer.to_python(value))


app = FastAPI()


@app.post("/prebuilt")
def prebuilt(payload: Any = Body(...)):
    return serializer.to_python(validator.validate_python(payload))


with TestClient(app) as client:
    response = client.post("/prebuilt", json={"inner": {"x": 9}})
    assert response.status_code == 200, response.text
    assert response.json() == {"inner": {"x": 9}}
    print("http", response.status_code, response.json())
