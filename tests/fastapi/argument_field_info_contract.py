from typing import Any, NamedTuple

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import TypeAdapter, validate_call
from pydantic_core import core_schema


def inspect_value(value: Any, info: core_schema.ValidationInfo):
    return {"value": value, "field_name": info.field_name, "data": info.data}


class Inspected:
    @classmethod
    def __get_pydantic_core_schema__(cls, source_type, handler):
        return core_schema.with_info_plain_validator_function(inspect_value)


class Pair(NamedTuple):
    first: int
    foobar: Inspected


@validate_call
def called(foobar: Inspected):
    return foobar


app = FastAPI()


@app.get("/argument-info")
def argument_info():
    pair = TypeAdapter(Pair).validate_python({"first": "2", "foobar": 3})
    return {"namedtuple": pair.foobar, "call": called(4)}


response = TestClient(app).get("/argument-info")
print(response.status_code, response.json())
