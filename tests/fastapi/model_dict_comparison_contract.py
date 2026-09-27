from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class Item(BaseModel):
    name: str


app = FastAPI()


@app.get('/model-dict-comparison')
def model_dict_comparison():
    item = Item(name='widget')
    payload = item.model_dump()
    return {
        'equal': item == payload,
        'different': item != payload,
        'same_model': item == Item(name='widget'),
    }


response = TestClient(app).get('/model-dict-comparison')
print(response.status_code, response.json())
