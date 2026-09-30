from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ConfigDict


class Item(BaseModel):
    model_config = ConfigDict(extra='allow')
    name: str


app = FastAPI()


@app.get('/model-extra-iteration')
def model_extra_iteration():
    item = Item.model_validate({'name': 'widget', 'quantity': 2})
    return {'items': list(item), 'dictionary': dict(item)}


response = TestClient(app).get('/model-extra-iteration')
print(response.status_code, response.json())
