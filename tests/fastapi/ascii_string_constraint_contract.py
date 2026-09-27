from typing import Annotated

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, StringConstraints


class Payload(BaseModel):
    value: Annotated[str, StringConstraints(ascii_only=True)]


app = FastAPI()


@app.post('/ascii')
def ascii_endpoint(payload: Payload):
    return {'value': payload.value}


client = TestClient(app)
for value in ('hello', 'café'):
    response = client.post('/ascii', json={'value': value})
    if response.status_code == 200:
        print(response.status_code, response.json())
    else:
        error = response.json()['detail'][0]
        print(response.status_code, error['type'], error['msg'])
