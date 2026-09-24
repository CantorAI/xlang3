import importlib
import sys
from pathlib import Path

from fastapi import FastAPI
from fastapi.testclient import TestClient


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "fixtures" / "core"))
app = FastAPI()


@app.get("/imports/circular")
def circular_import():
    try:
        importlib.import_module("circular_import_case.first")
    except ImportError as exc:
        message = str(exc)
        return {
            "partial": "cannot import name 'missing' from partially initialized module 'circular_import_case.first'" in message,
            "circular": "most likely due to a circular import" in message,
        }
    return {"partial": False, "circular": False}


with TestClient(app) as client:
    response = client.get("/imports/circular")
    assert response.status_code == 200, response.text
    assert response.json() == {"partial": True, "circular": True}, response.text
    print(response.status_code, response.json())
