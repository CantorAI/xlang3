import binascii

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import Base64Bytes, BaseModel


class Payload(BaseModel):
    data: Base64Bytes


app = FastAPI()


@app.post('/base64')
def base64_endpoint(payload: Payload):
    return {'length': len(payload.data)}


print(issubclass(binascii.Error, ValueError), issubclass(binascii.Incomplete, ValueError))
response = TestClient(app).post('/base64', json={'data': 'invalid-base64-bytes'})
error = response.json()['detail'][0]
print(response.status_code, error['type'], error['msg'])
