import asyncio
import threading

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/sync")
def sync_endpoint():
    return {"ok": True}


for _ in range(12):
    with TestClient(app) as client:
        response = client.get("/sync")
        assert response.status_code == 200
        assert response.json() == {"ok": True}
    del client, response

tasks = asyncio.tasks._scheduled_tasks
print("tasks", len(tasks.data), sum(reference() is not None for reference in tasks.data))
print("dangling", len(threading._dangling))
