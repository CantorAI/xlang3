"""CPython oracle for default SIGINT dispatch and signal handler identity."""

import json
import signal

from fastapi import FastAPI
from fastapi.testclient import TestClient


print("default-handler", signal.getsignal(signal.SIGINT) is signal.default_int_handler)
try:
    signal.raise_signal(signal.SIGINT)
except KeyboardInterrupt:
    print("raised", "KeyboardInterrupt")

app = FastAPI()


@app.get("/sigint-default")
def sigint_default():
    return {"handler_is_default": signal.getsignal(signal.SIGINT) is signal.default_int_handler}


response = TestClient(app).get("/sigint-default")
print("fastapi", response.status_code, json.dumps(response.json(), sort_keys=True))
