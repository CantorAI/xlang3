from collections.abc import Mapping

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ValidationError


class Payload(BaseModel):
    value: int


class Other(BaseModel):
    value: int


class MappingInput(Mapping):
    def __getitem__(self, key):
        return {'value': 42}[key]

    def __iter__(self):
        return iter(('value',))

    def __len__(self):
        return 1


app = FastAPI()


@app.get('/model-inputs')
def model_inputs():
    invalid = []
    for value in (Other(value=42), [('value', 42)]):
        try:
            Payload.model_validate(value)
        except ValidationError as error:
            invalid.append(error.errors()[0]['type'])
    return {'mapping': Payload.model_validate(MappingInput()).value,
            'invalid': invalid}


response = TestClient(app).get('/model-inputs')
print(response.status_code, response.json())
