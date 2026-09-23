from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, TypeAdapter
from pydantic.dataclasses import dataclass


@dataclass(slots=True)
class LineItem:
    quantity: int
    label: str = "unit"

    def __post_init__(self) -> None:
        self.label = self.label.upper()


class Order(BaseModel):
    item: LineItem


app = FastAPI()


@app.post("/orders", response_model=Order)
async def create_order(order: Order) -> Order:
    return order


with TestClient(app) as client:
    response = client.post("/orders", json={"item": {"quantity": "3"}})
    print(response.status_code, response.json())

    invalid = client.post("/orders", json={"item": {"quantity": "many"}})
    detail = invalid.json()["detail"][0]
    print(invalid.status_code, detail["type"], tuple(detail["loc"]))

adapter = TypeAdapter(LineItem)
item = adapter.validate_python({"quantity": "4", "label": "box"})
print(type(item).__name__, item.quantity, item.label)
print(adapter.dump_python(item), adapter.dump_json(item))
