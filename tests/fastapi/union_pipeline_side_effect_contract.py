from typing import Annotated

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import TypeAdapter, ValidationError
from pydantic.experimental.pipeline import validate_as


calls = []


def record(label):
    def step(value):
        calls.append((label, value))
        return value

    return step


adapter = TypeAdapter(
    Annotated[
        int,
        validate_as(int).transform(record('first')).gt(10).transform(record('first-ok'))
        | validate_as(int).transform(record('second')).lt(5).transform(record('second-ok')),
    ]
)


app = FastAPI()


@app.get('/union-pipeline/{value}')
def union_pipeline(value: int):
    calls.clear()
    try:
        result = adapter.validate_python(value)
    except ValidationError:
        result = None
    return {'result': result, 'calls': calls.copy()}


with TestClient(app) as client:
    for value in (1, 20, 9):
        response = client.get(f'/union-pipeline/{value}')
        print(value, response.status_code, response.json())
