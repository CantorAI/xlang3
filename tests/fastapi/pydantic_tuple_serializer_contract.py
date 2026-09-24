import warnings
from functools import partial

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic_core import SchemaSerializer, core_schema


def prefix_value(prefix, value, _info):
    return f"{prefix}{value}"


serializer = SchemaSerializer(
    {
        "type": "tuple",
        "items_schema": [
            core_schema.any_schema(
                serialization=core_schema.plain_serializer_function_ser_schema(
                    partial(prefix_value, prefix), info_arg=True
                )
            )
            for prefix in ("a", "b", "extra")
        ],
        "variadic_item_index": 2,
    }
)

for values in ((), (1,), (1, 2), (1, 2, 3), (1, 2, 3, 4)):
    with warnings.catch_warnings(record=True) as raised:
        warnings.simplefilter("always")
        python_result = serializer.to_python(values)
        json_result = serializer.to_python(values, mode="json")
        bytes_result = serializer.to_json(values)
    assert not raised, raised
    assert bytes_result.decode() == "[" + ",".join(
        '"' + item + '"' for item in json_result
    ) + "]"
    print(len(values), python_result)


app = FastAPI()


@app.get("/tuple/{size}")
def get_tuple(size: int) -> list[str]:
    return serializer.to_python(tuple(range(size)), mode="json")


with TestClient(app) as client:
    response = client.get("/tuple/1")
    assert response.status_code == 200
    print("http", response.status_code, response.json())
