from typing import ParamSpec, TypeVar, TypeVarTuple

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ConfigDict
from pydantic_core import SchemaSerializer, core_schema


class DictSubclass(dict):
    pass


class TupleSubclass(tuple):
    pass


type Inferred[T] = list[T]


class FrozenModel(BaseModel):
    model_config = ConfigDict(frozen=True)
    x: int


app = FastAPI()


@app.get('/generic-native-containers')
def generic_native_containers():
    dictionary = SchemaSerializer(core_schema.dict_schema(
        core_schema.str_schema(), core_schema.int_schema()))
    tuple_items = SchemaSerializer(core_schema.tuple_schema(
        [core_schema.int_schema(), core_schema.int_schema()]))
    parameters = (TypeVar('T'), ParamSpec('P', covariant=True), TypeVarTuple('Ts'))
    try:
        list[int][str]
    except TypeError as error:
        alias_error = str(error)
    return {
        'dict': dictionary.to_python(DictSubclass({'a': 1})),
        'tuple': tuple_items.to_python(TupleSubclass((2, 3))),
        'parameters': [repr(parameter) for parameter in parameters],
        'inferred': repr(Inferred.__value__),
        'alias_error': alias_error,
        'set_unique': len({FrozenModel(x=1), FrozenModel(x=1)}),
    }


response = TestClient(app).get('/generic-native-containers')
print(response.status_code, response.json())
