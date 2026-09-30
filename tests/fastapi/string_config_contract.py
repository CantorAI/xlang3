from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ConfigDict


class Payload(BaseModel):
    model_config = ConfigDict(str_strip_whitespace=True, str_to_lower=True)
    name: str


print('model', Payload(name='  ALICE  ').name)

app = FastAPI()


@app.post('/payload')
def payload(body: Payload):
    return {'name': body.name}


client = TestClient(app)
print('http', client.post('/payload', json={'name': '  BOB  '}).json())
