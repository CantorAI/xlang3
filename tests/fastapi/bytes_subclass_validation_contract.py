from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ConfigDict, TypeAdapter


class Child(bytes):
    def __new__(cls, data):
        return bytes.__new__(cls, data)


class Payload(BaseModel):
    data: bytes


app = FastAPI()


@app.get('/bytes')
def bytes_route():
    source = Child(b'foobar')
    validated = Payload(data=source)
    strict = TypeAdapter(bytes, config=ConfigDict(strict=True)).validate_python(source)
    return {
        'preserved': validated.data is source,
        'strict_preserved': strict is source,
        'length': len(validated.data),
        'repr': repr(validated.data),
    }


response = TestClient(app).get('/bytes')
print(response.status_code, response.json())
