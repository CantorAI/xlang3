from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


def make_payload():
    class Payload(BaseModel):
        cls: type[str | bytes]

    return Payload(cls=str)


app = FastAPI()


@app.get('/type')
def type_route():
    payload = make_payload()
    return {'name': payload.cls.__name__}


response = TestClient(app).get('/type')
print(response.status_code, response.json())
