from typing import Annotated

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, Field


class DynamicName:
    def __getattr__(self, name):
        if name == '__qualname__':
            return 'DynamicName'
        raise AttributeError(name)


instance = DynamicName()
print('qualname', instance.__qualname__, '__qualname__' in dir(instance))

Short = Annotated[str, Field(max_length=3)]
print('nested-repr', repr(list[Short]))


class Payload(BaseModel):
    items: list[Short]


print('field-repr', repr(Payload.model_fields['items']))

app = FastAPI()


@app.post('/payload')
def payload(body: Payload):
    return {'items': body.items}


client = TestClient(app)
print('valid', client.post('/payload', json={'items': ['abc']}).status_code)
print('invalid', client.post('/payload', json={'items': ['abcd']}).status_code)
