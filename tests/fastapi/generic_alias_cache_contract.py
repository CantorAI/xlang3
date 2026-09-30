from collections.abc import Callable, Iterable
from typing import Generic, TypeVar

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


T = TypeVar('T')


class Box(BaseModel, Generic[T]):
    value: T


Alias = Callable[[int], Iterable[str]]
First = Box[Alias]
Second = Box[Callable[[int], Iterable[str]]]

app = FastAPI()


@app.get('/generic-cache')
def generic_cache():
    return {'same_model': First is Second, 'alias_hash_equal': hash(Alias) == hash(Callable[[int], Iterable[str]])}


response = TestClient(app).get('/generic-cache')
print(response.status_code, response.json())
