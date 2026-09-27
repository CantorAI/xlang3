from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic_core import PydanticSerializationError, SchemaSerializer, core_schema


app = FastAPI()


@app.get("/serialization-error")
def serialization_error():
    serializer = SchemaSerializer(core_schema.any_schema())
    try:
        serializer.to_json(type)
    except PydanticSerializationError as error:
        return {"class_repr": repr(type), "error": str(error)}

    raise AssertionError("serializing a class should fail")


with TestClient(app) as client:
    response = client.get("/serialization-error")
    print(response.status_code, response.json())
