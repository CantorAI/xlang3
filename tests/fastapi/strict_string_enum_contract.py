from enum import Enum

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, StrictStr


class Fruit(str, Enum):
    banana = "banana"


class PlainFruit(Enum):
    banana = "banana"


class Payload(BaseModel):
    value: StrictStr


app = FastAPI()


@app.get("/strict-string-enum")
def strict_string_enum():
    item = Payload.model_validate({"value": Fruit.banana})
    return {"value": item.value, "type": type(item.value).__name__}


response = TestClient(app).get("/strict-string-enum")
print(response.status_code, response.json())
print(Payload.model_validate({"value": Fruit.banana}) == Payload.model_construct(value=Fruit.banana))
print(hash(Fruit.banana) == hash("banana"), "banana" in {Fruit.banana})
try:
    Payload.model_validate({"value": PlainFruit.banana})
except Exception as error:
    print(error.errors(include_url=False)[0]["type"])
