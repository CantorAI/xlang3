import json

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic_core import SchemaSerializer, core_schema


app = FastAPI()


@app.get("/tuple-union")
def tuple_union():
    number = core_schema.float_schema()
    serializer = SchemaSerializer(
        core_schema.union_schema(
            [
                core_schema.tuple_schema([number, number]),
                core_schema.tuple_schema([number, number, number]),
            ]
        )
    )
    return {
        "two": serializer.to_python((1.0, 2.0)),
        "three": serializer.to_python((1.0, 2.0, 3.0)),
        "encoded": serializer.to_json((1.0, 2.0, 3.0)).decode(),
    }


with TestClient(app) as client:
    response = client.get("/tuple-union")
    print(response.status_code, json.dumps(response.json(), sort_keys=True))
