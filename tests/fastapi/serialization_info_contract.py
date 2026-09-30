import json

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic_core import SchemaSerializer, core_schema


app = FastAPI()


@app.get("/serialization-info")
def serialization_info():
    observed = {"polymorphic": []}

    def serialize(value, info):
        observed["attributes"] = vars(info)
        observed["representation"] = repr(info)
        observed["polymorphic"].append(info.polymorphic_serialization)
        return value

    serializer = SchemaSerializer(
        core_schema.any_schema(
            serialization=core_schema.plain_serializer_function_ser_schema(
                serialize, info_arg=True
            )
        )
    )
    observed["value"] = serializer.to_python(4)
    serializer.to_python(4, polymorphic_serialization=True)
    serializer.to_python(4, polymorphic_serialization=False)
    return observed


with TestClient(app) as client:
    response = client.get("/serialization-info")
    print(response.status_code, json.dumps(response.json(), sort_keys=True))
