from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, InstanceOf, SerializeAsAny


class Inner(BaseModel):
    pass


class SubInner(Inner):
    x: int


class Payload(BaseModel):
    standard: InstanceOf[Inner]
    outer_any: SerializeAsAny[InstanceOf[Inner]]
    inner_any: InstanceOf[SerializeAsAny[Inner]]


app = FastAPI()


@app.get('/instanceof-any')
def instanceof_any():
    item = Payload(
        standard=SubInner(x=1),
        outer_any=SubInner(x=2),
        inner_any=SubInner(x=3),
    )
    return item.model_dump()


response = TestClient(app).get('/instanceof-any')
print(response.status_code, response.json())
