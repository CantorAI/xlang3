from typing import Union

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class Payload(BaseModel):
    values: Union[dict[str, str], list[str], dict[str, list[str]]]


app = FastAPI()


@app.get('/compound-union')
def compound_union():
    cases = [{'L': '1'}, ['L1'], {'x': ('pika',)}]
    return [Payload(values=case).model_dump()['values'] for case in cases]


response = TestClient(app).get('/compound-union')
print(response.status_code, response.json())
