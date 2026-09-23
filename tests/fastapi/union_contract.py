from fastapi import Body, FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel
from pydantic_core import SchemaError, SchemaValidator, ValidationError, core_schema


for choices in (
    [core_schema.int_schema(), core_schema.float_schema()],
    [core_schema.float_schema(), core_schema.int_schema()],
):
    validator = SchemaValidator(core_schema.union_schema(choices))
    for value in (1, 1.0):
        result = validator.validate_python(value)
        print(type(result).__name__, repr(result))

try:
    SchemaValidator(core_schema.union_schema([]))
except SchemaError as error:
    print(str(error))

single = SchemaValidator(core_schema.union_schema([core_schema.str_schema()]))
print(single.title, single.validate_python("text"))
try:
    single.validate_python(12)
except ValidationError as error:
    detail = error.errors(include_url=False)[0]
    print(detail["type"], detail["loc"])

custom = SchemaValidator(
    core_schema.union_schema(
        [core_schema.str_schema(), core_schema.bytes_schema()],
        custom_error_type="invalid_text",
        custom_error_message="Expected text or bytes",
    )
)
try:
    custom.validate_python(12)
except ValidationError as error:
    detail = error.errors(include_url=False)[0]
    print(detail["type"], detail["loc"], detail["msg"])

uuid_or_str = SchemaValidator(
    core_schema.union_schema([core_schema.str_schema(), core_schema.uuid_schema()])
)
json_value = uuid_or_str.validate_json('"12345678-1234-5678-1234-567812345678"')
print(type(json_value).__name__, str(json_value))


class CoreInt:
    pass


class CoreStr:
    pass


core_models = SchemaValidator(
    core_schema.union_schema(
        [
            core_schema.model_schema(
                CoreInt,
                core_schema.model_fields_schema(
                    {"x": core_schema.model_field(core_schema.int_schema())}
                ),
            ),
            core_schema.model_schema(
                CoreStr,
                core_schema.model_fields_schema(
                    {"x": core_schema.model_field(core_schema.str_schema())}
                ),
            ),
        ]
    )
)
print(type(core_models.validate_python({"x": "1"})).__name__)
try:
    core_models.validate_python({})
except ValidationError as error:
    print([item["loc"] for item in error.errors(include_url=False)])


class IntOnly(BaseModel):
    x: int


class StrOnly(BaseModel):
    x: str


class TwoInts(BaseModel):
    x: int
    y: int


app = FastAPI()


@app.post("/numbers")
def choose(value: int | float = Body()) -> dict[str, object]:
    return {"type": type(value).__name__, "value": value}


@app.post("/models")
def choose_model(value: IntOnly | StrOnly | TwoInts) -> dict[str, object]:
    return {"type": type(value).__name__, "data": value.model_dump()}


with TestClient(app) as client:
    for body in ("1", "1.0"):
        response = client.post(
            "/numbers", content=body, headers={"content-type": "application/json"}
        )
        print(response.status_code, response.json())
    for body in ({"x": "1"}, {"x": 1}, {"x": 1, "y": 2}):
        response = client.post("/models", json=body)
        print(response.status_code, response.json())
