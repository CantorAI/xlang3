import dataclasses

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ConfigDict, TypeAdapter
from pydantic.dataclasses import dataclass as pydantic_dataclass


@pydantic_dataclass
class DataBase:
    __pydantic_config__ = ConfigDict(polymorphic_serialization=True)
    a: int


@pydantic_dataclass
class DataChild(DataBase):
    b: str


@dataclasses.dataclass
class StandardBase:
    __pydantic_config__ = ConfigDict(polymorphic_serialization=True)
    a: int


@dataclasses.dataclass
class StandardChild(StandardBase):
    b: str


class ModelBase(BaseModel):
    model_config = ConfigDict(polymorphic_serialization=True)
    a: int


class ModelChild(ModelBase):
    b: str


app = FastAPI()


@app.get('/polymorphic')
def polymorphic():
    data_serializer = TypeAdapter(DataBase).serializer
    model_serializer = TypeAdapter(ModelBase).serializer
    standard_serializer = TypeAdapter(StandardBase).serializer
    data = DataChild(a=1, b='x')
    model = ModelChild(a=2, b='y')
    return {
        'data': data_serializer.to_python(data),
        'data_disabled': data_serializer.to_python(data, polymorphic_serialization=False),
        'data_json': data_serializer.to_json(data).decode(),
        'model': model_serializer.to_python(model),
        'model_disabled': model_serializer.to_python(model, polymorphic_serialization=False),
        'standard': standard_serializer.to_python(StandardChild(a=3, b='z')),
    }


response = TestClient(app).get('/polymorphic')
print(response.status_code, response.json())
