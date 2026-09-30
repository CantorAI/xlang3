from enum import Enum
import re

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import TypeAdapter


class Plain(Enum):
    a = 1


class Custom(Enum):
    a = 1

    @classmethod
    def _missing_(cls, value):
        return cls.a


app = FastAPI()


@app.get("/enum-missing")
def enum_missing():
    plain = TypeAdapter(Plain)
    custom = TypeAdapter(Custom)
    return {
        "plain": re.search(r"missing: (\w+)", repr(plain.validator)).group(1),
        "custom": re.search(r"missing: (\w+)", repr(custom.validator)).group(1),
        "validated": custom.validate_python(2).name,
    }


response = TestClient(app).get("/enum-missing")
print(response.status_code, response.json())
