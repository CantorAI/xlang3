import warnings
from typing import Literal, Union

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, Field, RootModel
from pydantic_core import SchemaSerializer, core_schema


class IModel(BaseModel):
    kind: Literal["IModel"]
    int_value: int


class RModel(RootModel):
    root: IModel


class SModel(BaseModel):
    kind: Literal["SModel"]
    str_value: str


class Model(RootModel):
    root: Union[SModel, RModel] = Field(discriminator="kind")


app = FastAPI()


def branch(tag):
    return core_schema.typed_dict_schema(
        {
            "kind": core_schema.typed_dict_field(
                core_schema.literal_schema([tag])
            ),
            "value": core_schema.typed_dict_field(core_schema.int_schema()),
        }
    )


raw_serializer = SchemaSerializer(
    core_schema.tagged_union_schema(
        {"a": branch("a"), "b": branch("b")}, discriminator="kind"
    )
)


@app.get("/tagged-union-fallback")
def tagged_union_fallback():
    model = Model({"kind": "IModel", "int_value": 42})
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        payload = model.model_dump()
    without_warnings = model.model_dump(warnings=False)
    try:
        model.model_dump(warnings="error")
    except Exception as exc:
        error_type = type(exc).__name__
    else:
        error_type = "missing-error"
    raw_cases = []
    for value in ({"kind": "c", "value": 7}, {"value": 7}):
        with warnings.catch_warnings(record=True) as raw_warnings:
            warnings.simplefilter("always")
            raw_serializer.to_python(value)
        raw_cases.append(
            [warning.category.__name__ for warning in raw_warnings]
            + [str(raw_warnings[0].message).count("PydanticSerializationUnexpectedValue")]
        )
    return {
        "payload": payload,
        "without_warnings": without_warnings,
        "warnings": [warning.category.__name__ for warning in caught],
        "warning_reason": "failed to get discriminator value"
        in str(caught[0].message) if caught else False,
        "error_type": error_type,
        "raw_cases": raw_cases,
    }


response = TestClient(app).get("/tagged-union-fallback")
print(response.status_code, response.json())
