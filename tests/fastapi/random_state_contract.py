import json
import random

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/random-state")
def random_state() -> dict:
    generator = random.Random((1 << 130) + 90314)
    bits = [generator.getrandbits(count) for count in (1, 32, 33, 100)]
    state = generator.getstate()
    restored = random.Random()
    restored.setstate(state)
    return {
        "bits": bits,
        "float": generator.random(),
        "restored_float": restored.random(),
        "state_words": len(state[1]),
    }


response = TestClient(app).get("/random-state")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
