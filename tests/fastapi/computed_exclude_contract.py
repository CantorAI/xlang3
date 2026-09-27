from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, computed_field


class Payload(BaseModel):
    foo: int

    @computed_field(exclude_if=lambda value: value == 1)
    def bar(self) -> int:
        return self.foo

    @computed_field(exclude_if=lambda value: value == 2)
    def baz(self) -> int:
        return self.foo


print('dump-one', Payload(foo=1).model_dump())
print('dump-two', Payload(foo=2).model_dump())
print('json-one', Payload(foo=1).model_dump_json())

app = FastAPI()


@app.get('/payload/{value}', response_model=Payload)
def payload(value: int):
    return Payload(foo=value)


client = TestClient(app)
print('http-one', client.get('/payload/1').json())
print('http-two', client.get('/payload/2').json())
