from enum import Enum

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class ListEnum(list[int], Enum):
    first = [123]
    second = [456]


class EnumModel(BaseModel):
    item: ListEnum


app = FastAPI()


@app.get('/generic-enum-schema')
def generic_enum_schema():
    return {
        'base': ListEnum.__bases__[0].__name__,
        'original': repr(ListEnum.__orig_bases__[0]),
        'value': ListEnum.first.value,
        'schema': EnumModel.model_json_schema(),
    }


response = TestClient(app).get('/generic-enum-schema')
print(response.status_code, response.json())
