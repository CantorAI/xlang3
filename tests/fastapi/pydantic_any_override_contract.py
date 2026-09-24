import operator
import warnings
from enum import Enum

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic_core import SchemaSerializer, core_schema


class Choice(Enum):
    A = (1,)
    B = (2,)


serializer = SchemaSerializer(
    core_schema.no_info_after_validator_function(
        operator.attrgetter("value"),
        core_schema.enum_schema(Choice, list(Choice.__members__.values())),
        serialization=core_schema.simple_ser_schema("any"),
    )
)

with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter("always")
    python_value = serializer.to_python({Choice.A: "x"})
    json_value = serializer.to_python({Choice.A: "x"}, mode="json")
    json_bytes = serializer.to_json({Choice.A: "x"})
    integer = serializer.to_python(1)
assert not caught, caught
assert python_value == {Choice.A: "x"}
assert json_bytes == b'{"1":"x"}'
print("any", json_value, integer)


app = FastAPI()


@app.get("/choice")
def get_choice() -> dict[str, str]:
    return serializer.to_python({Choice.A: "x"}, mode="json")


with TestClient(app) as client:
    response = client.get("/choice")
    assert response.status_code == 200, response.text
    print("http", response.status_code, response.json())
