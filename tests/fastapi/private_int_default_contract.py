from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, PrivateAttr


class Number(int):
    pass


default = Number(7)
default.label = "seven"


class Model(BaseModel):
    _number: int = PrivateAttr(default=default)


app = FastAPI()


@app.get("/private-int-default")
def private_int_default():
    model = Model()
    value = model._number
    return {"value": int(value), "type": type(value).__name__, "label": value.label}


response = TestClient(app).get("/private-int-default")
print(response.status_code, response.json())
