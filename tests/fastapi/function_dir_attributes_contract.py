"""CPython oracle for user-defined function attributes in dir()."""

import json

from fastapi import FastAPI
from fastapi.testclient import TestClient


def handler():
    return "ready"


handler.marker = "registered"
print("function-dir", "marker" in dir(handler), handler.marker)

app = FastAPI()


@app.get("/function-dir")
def function_dir():
    return {"marker_visible": "marker" in dir(handler), "result": handler()}


response = TestClient(app).get("/function-dir")
print("fastapi", response.status_code, json.dumps(response.json(), sort_keys=True))
