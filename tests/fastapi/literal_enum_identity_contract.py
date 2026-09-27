from enum import Enum
from typing import Literal

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class Bar(str, Enum):
    FIZ = "fiz"
    FUZ = "fuz"


class Foo(int, Enum):
    ONE = 1


class Payload(BaseModel):
    single: Literal[Bar.FIZ]
    multiple: Literal[Bar.FIZ, Bar.FUZ]


class MixedPayload(BaseModel):
    enum_first: Literal[Foo.ONE, 1]
    int_first: Literal[1, Foo.ONE]


app = FastAPI()


@app.get("/literal-enum")
def literal_enum():
    item = Payload.model_validate({"single": "fiz", "multiple": "fuz"})
    return {
        "single_identity": item.single is Bar.FIZ,
        "multiple_identity": item.multiple is Bar.FUZ,
        "single_type": type(item.single).__name__,
        "multiple_type": type(item.multiple).__name__,
    }


@app.post('/literal-mixed')
def literal_mixed(item: MixedPayload):
    return {
        'enum_first_type': type(item.enum_first).__name__,
        'int_first_type': type(item.int_first).__name__,
    }


response = TestClient(app).get("/literal-enum")
print(response.status_code, response.json())
from_enum = MixedPayload.model_validate(
    {'enum_first': Foo.ONE, 'int_first': Foo.ONE}
)
print(type(from_enum.enum_first).__name__, type(from_enum.int_first).__name__)
mixed_response = TestClient(app).post(
    '/literal-mixed', json={'enum_first': 1, 'int_first': 1}
)
print(mixed_response.status_code, mixed_response.json())
