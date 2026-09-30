from collections.abc import Hashable

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel
from pydantic_core import PydanticSerializationError


class Payload(BaseModel):
    value: Hashable


class HashableObject:
    def __hash__(self):
        return 0


app = FastAPI()


@app.get('/serialize')
def serialize():
    valid = Payload(value=(1, 2)).model_dump_json()
    try:
        Payload(value=HashableObject()).model_dump_json()
    except PydanticSerializationError as error:
        invalid = str(error)
    else:
        invalid = 'no error'
    return {'valid': valid, 'invalid': invalid}


response = TestClient(app).get('/serialize')
data = response.json()
print(response.status_code, data['valid'])
print(data['invalid'])
