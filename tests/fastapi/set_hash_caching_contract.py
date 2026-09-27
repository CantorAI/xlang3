import json

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/set-hashes")
def set_hashes():
    calls = []

    class Key:
        def __init__(self, name):
            self.name = name

        def __hash__(self):
            calls.append(self.name)
            return 19

        def __eq__(self, other):
            return self is other

    first = Key("first")
    second = Key("second")
    values = {first}
    values.add(second)
    add_calls = calls[:]
    calls.clear()
    contains = second in values
    contains_calls = calls[:]
    calls.clear()
    values.discard(first)
    return {
        "add_calls": add_calls,
        "contains": contains,
        "contains_calls": contains_calls,
        "discard_calls": calls,
        "count": len(values),
    }


response = TestClient(app).get("/set-hashes")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
