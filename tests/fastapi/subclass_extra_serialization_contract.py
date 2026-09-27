from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ConfigDict


class Parent(BaseModel):
    x: int


class Child(Parent):
    model_config = ConfigDict(extra="allow")


class Container(BaseModel):
    inner: Parent


app = FastAPI()


@app.get("/subclass-extra-serialization")
def subclass_extra_serialization():
    child = Child(x=1, y=2)
    container = Container(inner=child)
    return {
        "child": child.model_dump(),
        "container": container.model_dump(),
        "as_any": container.model_dump(serialize_as_any=True),
    }


response = TestClient(app).get("/subclass-extra-serialization")
print(response.status_code, response.json())
