from enum import Enum
from typing import NamedTuple

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class Pair(NamedTuple):
    x: int
    y: int


class Payload(BaseModel):
    pair: Pair


print('instance', Payload.model_validate({'pair': Pair(2, 3)}).pair)

app = FastAPI()


@app.post('/pair')
def pair(body: Payload):
    return {'sum': body.pair.x + body.pair.y}


client = TestClient(app)
print('http', client.post('/pair', json={'pair': [2, 3]}).json())


class Name(NamedTuple):
    f: str


class Choice(Name, Enum):
    FOO = 'foo'


class EnumPayload(BaseModel, use_enum_values=True):
    value: Choice


@app.get('/namedtuple-enum')
def namedtuple_enum():
    item = EnumPayload(value=Name('foo'))
    return {'value': item.value.f, 'same_hash': hash(Name('foo')) == hash(Choice.FOO.value)}


enum_response = client.get('/namedtuple-enum')
print('enum', enum_response.status_code, enum_response.json())
