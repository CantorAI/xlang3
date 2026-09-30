import json

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic_core import SchemaSerializer, core_schema


app = FastAPI()


@app.get("/json-round-trip")
def json_round_trip():
    serializer = SchemaSerializer(
        core_schema.any_schema(
            serialization=core_schema.simple_ser_schema("json")
        )
    )
    value = {1: 2}
    return {
        "json": serializer.to_python(value, mode="json"),
        "round_trip": serializer.to_python(value, mode="json", round_trip=True),
        "encoded": serializer.to_json(value).decode(),
    }


with TestClient(app) as client:
    response = client.get("/json-round-trip")
    print(response.status_code, json.dumps(response.json(), sort_keys=True))
