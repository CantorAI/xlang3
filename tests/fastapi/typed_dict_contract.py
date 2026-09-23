from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ConfigDict
from pydantic_core import CoreConfig, SchemaError, SchemaValidator, ValidationError, core_schema


field = core_schema.typed_dict_field(core_schema.int_schema())
allowed = SchemaValidator(
    core_schema.typed_dict_schema(
        fields={"item": field}, config=CoreConfig(extra_fields_behavior="allow")
    )
)
print(allowed.validate_python({"item": "2", "note": "ok"}))
try:
    allowed.validate_python({"item": "2", b"note": "ok"})
except ValidationError as error:
    detail = error.errors(include_url=False)[0]
    print(detail["type"], detail["loc"], detail["input"])

alias = SchemaValidator(
    core_schema.typed_dict_schema(
        fields={
            "item": core_schema.typed_dict_field(
                core_schema.int_schema(), validation_alias="Item"
            )
        },
        config=CoreConfig(validate_by_alias=True, validate_by_name=True),
    )
)
print(alias.validate_python({"item": "3"}))

try:
    SchemaValidator(
        core_schema.typed_dict_schema(
            fields={
                "item": core_schema.typed_dict_field(
                    core_schema.with_default_schema(core_schema.int_schema(), default=1),
                    required=True,
                )
            }
        )
    )
except SchemaError as error:
    print(str(error))


class Payload(BaseModel):
    model_config = ConfigDict(extra="allow")
    item: int


app = FastAPI()


@app.post("/items")
def accept(payload: Payload) -> dict[str, object]:
    return payload.model_dump()


with TestClient(app) as client:
    response = client.post("/items", json={"item": "2", "note": "ok"})
    print(response.status_code, response.json())
