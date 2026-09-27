import json

from fastapi import FastAPI
from fastapi.testclient import TestClient


class Item:
    def __init__(self, value):
        self.value = value

    def __hash__(self):
        return hash(self.value)

    def __eq__(self, other):
        return isinstance(other, Item) and self.value == other.value


app = FastAPI()


@app.get("/set-difference")
def set_difference():
    left = {Item(11), Item(12)}
    right = {Item(11)}
    return {
        "operator": sorted(item.value for item in left - right),
        "method": sorted(item.value for item in left.difference(right)),
        "membership": sorted(item.value for item in left if item in right),
        "non_set": set.__sub__({1}, [1]) is NotImplemented,
    }


response = TestClient(app).get("/set-difference")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
