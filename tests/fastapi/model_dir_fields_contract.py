from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class Item(BaseModel):
    name: str
    count: int = 2


app = FastAPI()


@app.get('/model-dir-fields')
def model_dir_fields():
    item = Item(name='widget')
    names = dir(item)
    return {
        'name': 'name' in names,
        'count': 'count' in names,
        'model_dump': 'model_dump' in names,
    }


response = TestClient(app).get('/model-dir-fields')
print(response.status_code, response.json())
