import warnings

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic_core import (
    PydanticSerializationUnexpectedValue,
    SchemaSerializer,
    core_schema,
)


def describe(value, _info):
    if value == "unexpected":
        raise PydanticSerializationUnexpectedValue()
    return f"func: {value!r}"


function_union = SchemaSerializer(
    core_schema.union_schema(
        [
            core_schema.any_schema(
                serialization=core_schema.plain_serializer_function_ser_schema(
                    describe, info_arg=True
                )
            ),
            core_schema.float_schema(
                serialization=core_schema.format_ser_schema("_^14")
            ),
        ]
    )
)
pet_union = SchemaSerializer(
    core_schema.union_schema(
        [
            core_schema.typed_dict_schema(
                {
                    "pet_type": core_schema.typed_dict_field(
                        core_schema.literal_schema(["cat"])
                    ),
                    "sound": core_schema.typed_dict_field(
                        core_schema.int_schema(
                            serialization=core_schema.format_ser_schema("04d")
                        )
                    ),
                }
            ),
            core_schema.typed_dict_schema(
                {
                    "pet_type": core_schema.typed_dict_field(
                        core_schema.literal_schema(["dog"])
                    ),
                    "sound": core_schema.typed_dict_field(
                        core_schema.float_schema(
                            serialization=core_schema.format_ser_schema("0.3f")
                        )
                    ),
                }
            ),
        ]
    )
)

with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter("always")
    assert function_union.to_python("foobar") == "func: 'foobar'"
    python_fallback = function_union.to_python("unexpected")
    json_fallback = function_union.to_python("unexpected", mode="json")
    assert function_union.to_json("unexpected") == b'"__unexpected__"'
    cat = pet_union.to_python({"pet_type": "cat", "sound": 3}, mode="json")
    dog = pet_union.to_python({"pet_type": "dog", "sound": 3}, mode="json")
assert not caught, caught
print("function", python_fallback, json_fallback)
print("pets", cat, dog)


app = FastAPI()


@app.get("/pet/{kind}")
def get_pet(kind: str) -> dict[str, str]:
    return pet_union.to_python({"pet_type": kind, "sound": 3}, mode="json")


with TestClient(app) as client:
    response = client.get("/pet/dog")
    assert response.status_code == 200, response.text
    print("http", response.status_code, response.json())
