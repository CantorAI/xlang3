from typing import Any, Generic, TypeVar

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ValidationError


T = TypeVar('T')


class Inner(BaseModel, Generic[T]):
    value: T


class Outer(BaseModel, Generic[T]):
    inner: Inner[T]


class Unrelated(BaseModel):
    value: int


app = FastAPI()


@app.get('/generic-model-revalidation')
def generic_model_revalidation():
    source = Inner[Any](value=7)
    result = Outer[int](inner=source)
    try:
        Outer[int](inner=Inner[str](value='bad'))
    except ValidationError as error:
        invalid = error.errors(include_url=False)[0]
    try:
        Outer[int](inner=Unrelated(value=7))
    except ValidationError as error:
        unrelated = error.errors(include_url=False)[0]
    return {
        'class': type(result.inner).__name__,
        'value': result.inner.value,
        'new_instance': result.inner is not source,
        'invalid_type': invalid['type'],
        'invalid_location': list(invalid['loc']),
        'unrelated_type': unrelated['type'],
    }


response = TestClient(app).get('/generic-model-revalidation')
print(response.status_code, response.json())
