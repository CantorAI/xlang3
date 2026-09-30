import json
from typing import ClassVar, Union

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, OnErrorOmit, TypeAdapter, field_serializer
from typing_extensions import TypedDict


class Payload(BaseModel):
    a: int
    b: int
    serializer_calls: ClassVar[int] = 0

    @field_serializer('a')
    def serialize_a(self, value: int) -> str:
        type(self).serializer_calls += 1
        return str(value)


class Inner(TypedDict):
    payload: Union[Payload, int]


class Outer(TypedDict):
    inner: Union[Inner, int]


app = FastAPI()


@app.get('/nested-union')
def nested_union():
    Payload.serializer_calls = 0
    payload = Payload.model_construct(a=1, b=False)
    encoded = TypeAdapter(Union[Outer, int]).dump_json(
        Outer(inner=Inner(payload=payload)), warnings=False
    )
    choices = TypeAdapter(list[Union[OnErrorOmit[int], OnErrorOmit[bool]]])
    validated = choices.validate_python([1, 'True', 'foo', '2', False, 'bar'])
    return {
        'encoded': json.loads(encoded),
        'serializer_calls': Payload.serializer_calls,
        'validated': validated,
    }


response = TestClient(app).get('/nested-union')
print(response.status_code, response.json())
