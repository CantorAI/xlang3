import gc
import json
import sys

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/gc-generator-referrers")
async def gc_generator_referrers():
    target = []
    owner = sys._getframe().f_generator
    referrers = gc.get_referrers(target)
    return {
        "owner_type": type(owner).__name__,
        "owner_is_referrer": any(item is owner for item in referrers),
        "referrer_types": sorted(type(item).__name__ for item in referrers),
    }


response = TestClient(app).get("/gc-generator-referrers")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
