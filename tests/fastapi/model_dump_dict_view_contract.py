from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class Payload(BaseModel):
    a: int
    b: int
    c: int = 3


app = FastAPI()


@app.post('/dump')
def dump(payload: Payload):
    include = {'a': 1}.keys()
    exclude = {'b': 1}.keys()
    return {'include': payload.model_dump(include=include),
            'exclude': payload.model_dump(exclude=exclude),
            'unset': payload.model_dump(exclude=exclude, exclude_unset=True)}


response = TestClient(app).post('/dump', json={'a': 1, 'b': 2})
print(response.status_code, response.json())
