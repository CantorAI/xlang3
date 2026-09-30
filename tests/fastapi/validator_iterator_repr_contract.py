from collections.abc import Iterable
from typing import Any

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import TypeAdapter


app = FastAPI()


@app.get("/iterator-repr")
def iterator_repr():
    return {
        "int": repr(TypeAdapter(Iterable[int]).validate_python([1])),
        "str": repr(TypeAdapter(Iterable[str]).validate_python(["a"])),
        "any": repr(TypeAdapter(Iterable[Any]).validate_python([1])),
        "list": repr(TypeAdapter(Iterable[list[int]]).validate_python([[1]])),
    }


response = TestClient(app).get("/iterator-repr")
print(response.status_code)
for key, value in response.json().items():
    print(key, value)
