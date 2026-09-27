from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ByteSize


class Payload(BaseModel):
    size: ByteSize


app = FastAPI()


@app.get('/bytesize')
def bytesize():
    payload = Payload(size='1 GiB')
    return {'mib': payload.size.to('MiB'), 'bytes': int(payload.size)}


response = TestClient(app).get('/bytesize')
print(response.status_code, response.json())
