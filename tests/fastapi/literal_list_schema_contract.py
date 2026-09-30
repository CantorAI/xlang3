from typing import Literal

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class LiteralModel(BaseModel):
    values: Literal[['a', 1]]


app = FastAPI()


@app.get('/literal-list-schema')
def literal_list_schema():
    return LiteralModel.model_json_schema()['properties']['values']


response = TestClient(app).get('/literal-list-schema')
print(response.status_code, response.json())
