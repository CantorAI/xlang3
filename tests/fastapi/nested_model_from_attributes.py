from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ValidationError


class Item(BaseModel):
    name: str
    price: float


try:
    Item.model_validate("name=Foo&price=50.5", from_attributes=True)
except ValidationError as error:
    print(error.errors()[0]["type"])


app = FastAPI()


@app.post("/items/")
async def create_item(item: Item):
    return item


with TestClient(app) as client:
    response = client.post("/items/", data={"name": "Foo", "price": "50.5"})
    assert response.status_code == 422
    detail = response.json()["detail"][0]
    print(response.status_code, detail["type"], detail["loc"], detail["input"])
