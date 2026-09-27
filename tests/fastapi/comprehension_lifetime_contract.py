import gc
import json
import weakref

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


class Token:
    pass


@app.get('/comprehension-lifetime')
def comprehension_lifetime():
    token = Token()
    reference = weakref.ref(token)
    result = [item for item in [token]]
    del result, token
    gc.collect()
    functions = [lambda: item for item in [1, 2]]
    return {
        'target_released': reference() is None,
        'closure_values': [function() for function in functions],
    }


response = TestClient(app).get('/comprehension-lifetime')
print(response.status_code, json.dumps(response.json(), sort_keys=True))
