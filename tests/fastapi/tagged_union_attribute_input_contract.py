from typing import Annotated, Literal

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, Field, ValidationError


class Cat(BaseModel):
    kind: Literal['cat']


class Dog(BaseModel):
    kind: Literal['dog']


class Payload(BaseModel):
    pet: Annotated[Cat | Dog, Field(discriminator='kind')]


app = FastAPI()


@app.post('/pets')
def pets(payload: Payload):
    return {'kind': payload.pet.kind}


client = TestClient(app)
for value in ('fish', {'unknown': 1}):
    response = client.post('/pets', json={'pet': value})
    print('http', response.status_code, response.json()['detail'][0]['type'])

for value in ('fish', 5, [], (), None, {'unknown': 1}):
    try:
        Payload.model_validate({'pet': value}, from_attributes=True)
    except ValidationError as error:
        print('python', repr(value), error.errors(include_url=False)[0]['type'])
