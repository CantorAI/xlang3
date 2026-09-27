import json
import warnings

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic_core import SchemaSerializer, core_schema


class BasicModel:
    def __init__(self, **fields):
        self.__dict__.update(fields)


class AModel(BasicModel):
    pass


class BModel(BasicModel):
    pass


app = FastAPI()


@app.get("/missing-model-field-warning")
def missing_model_field_warning():
    def variant(model, tag):
        return core_schema.model_schema(
            model,
            core_schema.model_fields_schema(
                {
                    "type": core_schema.model_field(core_schema.literal_schema([tag])),
                    tag: core_schema.model_field(core_schema.int_schema()),
                }
            ),
        )

    serializer = SchemaSerializer(
        core_schema.model_schema(
            BasicModel,
            core_schema.model_fields_schema(
                {
                    "root": core_schema.model_field(
                        core_schema.tagged_union_schema(
                            choices={"a": variant(AModel, "a"), "b": variant(BModel, "b")},
                            discriminator="type",
                        )
                    )
                }
            ),
        )
    )
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        value = serializer.to_python(BasicModel(root=AModel(type="a")))

    return {
        "value": value,
        "warnings": [
            {
                "category": warning.category.__name__,
                "missing_field": "Expected 2 fields but got 1: Expected `AModel`"
                in str(warning.message),
            }
            for warning in caught
        ],
    }


with TestClient(app) as client:
    response = client.get("/missing-model-field-warning")
    print(response.status_code, json.dumps(response.json(), sort_keys=True))
