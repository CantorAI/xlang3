from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class Item(BaseModel):
    value: int


app = FastAPI()


@app.get('/type-subclasses')
def type_subclasses():
    return {
        'object_has_type': type in type.__subclasses__(object),
        'base_model_has_item': Item in type.__subclasses__(BaseModel),
    }


response = TestClient(app).get('/type-subclasses')
print(response.status_code, response.json())
