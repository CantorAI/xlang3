from typing import Annotated, Literal

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, Field
from pydantic_core import SchemaValidator, ValidationError, core_schema


apple = core_schema.typed_dict_schema(
    fields={
        "kind": core_schema.typed_dict_field(core_schema.literal_schema(["apple"])),
        "count": core_schema.typed_dict_field(core_schema.int_schema()),
    }
)
banana = core_schema.typed_dict_schema(
    fields={
        "kind": core_schema.typed_dict_field(core_schema.literal_schema(["banana"])),
        "count": core_schema.typed_dict_field(core_schema.int_schema()),
    }
)
validator = SchemaValidator(
    core_schema.tagged_union_schema(
        choices={"apple": apple, "banana": banana}, discriminator="kind"
    )
)

for value in ({"count": 1}, {"kind": "pear", "count": 1}):
    try:
        validator.validate_python(value)
    except ValidationError as error:
        detail = error.errors(include_url=False)[0]
        print(detail["type"], detail["loc"], detail["ctx"])

custom = SchemaValidator(
    core_schema.tagged_union_schema(
        choices={"apple": apple, "banana": banana},
        discriminator="kind",
        custom_error_type="invalid_fruit",
        custom_error_message="Unknown fruit",
    )
)
try:
    custom.validate_python({"kind": "pear", "count": 1})
except ValidationError as error:
    detail = error.errors(include_url=False)[0]
    print(detail["type"], detail["loc"], detail["msg"])


class Apple(BaseModel):
    kind: Literal["apple"]
    count: int


class Banana(BaseModel):
    kind: Literal["banana"]
    count: int


class Order(BaseModel):
    fruit: Annotated[Apple | Banana, Field(discriminator="kind")]


app = FastAPI()


@app.post("/orders")
def create_order(order: Order) -> dict[str, object]:
    return {"fruit": order.fruit.kind, "count": order.fruit.count}


with TestClient(app) as client:
    accepted = client.post("/orders", json={"fruit": {"kind": "banana", "count": "2"}})
    print(accepted.status_code, accepted.json())
    rejected = client.post("/orders", json={"fruit": {"kind": "pear", "count": 1}})
    detail = rejected.json()["detail"][0]
    print(rejected.status_code, detail["type"], detail["loc"], detail["ctx"])
