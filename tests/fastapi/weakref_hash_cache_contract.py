import json
import weakref

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/weakref-hash")
def weakref_hash():
    class Target:
        def __init__(self):
            self.hash_calls = 0

        def __hash__(self):
            self.hash_calls += 1
            return 31

    target = Target()
    ref = weakref.ref(target)
    return {"first": hash(ref), "second": hash(ref), "calls": target.hash_calls}


response = TestClient(app).get("/weakref-hash")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
