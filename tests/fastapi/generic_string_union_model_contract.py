from typing import Generic, TypeVar, Union, get_args

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


T = TypeVar('T')


class Payload(BaseModel, Generic[T]):
    data: Union[T, int]


class Item(BaseModel):
    name: str
    payload: Payload['Item']


specialized = Item.model_fields['payload'].annotation
specialized.model_rebuild()

app = FastAPI()


@app.get('/generic-string-union', response_model=Item)
def generic_string_union():
    return {'name': 'outer', 'payload': {'data': {'name': 'inner', 'payload': {'data': 1}}}}


response = TestClient(app).get('/generic-string-union')
print(get_args(specialized.model_fields['data'].annotation)[0] is Item)
print(response.status_code, response.json())
