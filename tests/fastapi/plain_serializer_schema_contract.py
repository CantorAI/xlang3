from typing import Annotated

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, PlainSerializer


app = FastAPI()


@app.get('/plain-serializer-schema')
def plain_serializer_schema():
    class Model(BaseModel):
        value: Annotated[str, PlainSerializer(lambda item: f'serialized-{item}', return_type=str)] = 'foo'

    validation = Model.model_json_schema(mode='validation')['properties']['value']['default']
    serialization = Model.model_json_schema(mode='serialization')['properties']['value']['default']
    return {'validation': validation, 'serialization': serialization}


response = TestClient(app).get('/plain-serializer-schema')
print(response.status_code, response.json())
