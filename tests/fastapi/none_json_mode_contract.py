from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, Json, ValidationError


class Payload(BaseModel):
    value: Json[type(None)]


app = FastAPI()


@app.post('/none-json')
def none_json(payload: Payload):
    return {'value': payload.value}


try:
    Payload.model_validate({'value': '"a"'})
except ValidationError as error:
    print(error.errors(include_url=False)[0]['msg'])

response = TestClient(app).post('/none-json', json={'value': '"a"'})
print(response.status_code, response.json()['detail'][0]['msg'])

try:
    Payload.model_validate_json('{"value": "\\"a\\""}')
except ValidationError as error:
    print(error.errors(include_url=False)[0]['msg'])
