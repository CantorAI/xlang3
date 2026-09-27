import warnings
from typing import Annotated

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, Field


class Payload(BaseModel):
    value: Annotated[int, Field(deprecated='value is deprecated')]
    model_config = {'validate_assignment': True}


app = FastAPI()


@app.post('/update')
def update(payload: Payload):
    payload.value = payload.value + 1
    return {'value': payload.value}


with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter('always', DeprecationWarning)
    response = TestClient(app).post('/update', json={'value': 2})

print('response', response.status_code, response.json())
print('warnings', [str(item.message) for item in caught
                   if isinstance(item.message, DeprecationWarning)])
