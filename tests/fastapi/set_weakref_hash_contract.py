import weakref
from concurrent.futures import ThreadPoolExecutor

from fastapi import FastAPI
from fastapi.testclient import TestClient


class Item:
    pass


def make_weakset():
    item = Item()
    reference = weakref.ref(item)
    entries = weakref.WeakSet([item])
    return reference, entries


app = FastAPI()


@app.get("/weakset")
def weakset_cleanup():
    reference, entries = make_weakset()
    return {"dead": reference() is None, "remaining": len(entries)}


with TestClient(app) as client:
    response = client.get("/weakset")
    assert response.status_code == 200
    assert response.json() == {"dead": True, "remaining": 0}
    print(response.status_code, response.json())


def request_weakset(_):
    with TestClient(app) as client:
        response = client.get("/weakset")
        return response.status_code, response.json()


with ThreadPoolExecutor(max_workers=4) as executor:
    results = list(executor.map(request_weakset, range(16)))
assert results == [(200, {"dead": True, "remaining": 0})] * 16
print("weakset concurrent", len(results))
