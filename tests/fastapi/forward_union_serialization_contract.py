from typing import Annotated, Union

import annotationlib
from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, computed_field
from pydantic.functional_serializers import PlainSerializer


class Bar(BaseModel):
    bar_id: int


class Baz(Bar):
    baz_id: int


class Foo(BaseModel):
    items: Union[list["Foo"], list[Bar]]


class Computed(BaseModel):
    x: int

    @computed_field
    @property
    def two_x(self) -> Annotated[
        int, PlainSerializer(lambda value: f"double={value}", return_type=str)
    ]:
        return self.x * 2


app = FastAPI()


@app.get("/forward-union")
def forward_union():
    foo = Foo(items=[Baz(bar_id=1, baz_id=2), Baz(bar_id=3, baz_id=4)])
    nested = Foo(items=[Foo(items=[Baz(bar_id=42, baz_id=99)])])
    return {"direct": foo.model_dump(), "nested": nested.model_dump()}


@app.get("/computed")
def computed():
    return Computed(x=2).model_dump()


def first_forwardref() -> Annotated[
    int, PlainSerializer(lambda value: f"double={value}", return_type=str)
]:
    return 0


annotation = annotationlib.get_annotations(
    first_forwardref, format=annotationlib.Format.FORWARDREF
)["return"]
serializer = annotation.__metadata__[0].func
print(type(serializer(2)).__name__, serializer(2))

client = TestClient(app)
for path in ("/forward-union", "/computed"):
    response = client.get(path)
    print(response.status_code, response.json())
