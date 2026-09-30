import re
from typing import Any

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, field_serializer


class StringReturn(BaseModel):
    x: int

    @field_serializer("x", return_type=str)
    def serialize_x(self, value) -> str:
        return repr(value)


class AnyReturn(BaseModel):
    x: int

    @field_serializer("x", return_type=Any)
    def serialize_x(self, value) -> str:
        return repr(value)


app = FastAPI()


@app.get("/serializer-return-repr")
def serializer_return_repr():
    return {
        "string": re.search(
            r"return_serializer: *\w+",
            repr(StringReturn.__pydantic_serializer__),
        ).group(0),
        "any": re.search(
            r"return_serializer: *\w+",
            repr(AnyReturn.__pydantic_serializer__),
        ).group(0),
    }


response = TestClient(app).get("/serializer-return-repr")
print(response.status_code, response.json())
