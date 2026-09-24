import asyncio
import threading
import time

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/thread-handoff")
async def thread_handoff():
    loop = asyncio.get_running_loop()

    def worker():
        for _ in range(128):
            completed = threading.Event()
            loop.call_soon_threadsafe(completed.set)
            completed.wait()

    start = time.monotonic()
    await asyncio.to_thread(worker)
    return {"handoffs": 128, "under_ten_seconds": time.monotonic() - start < 10}


with TestClient(app) as client:
    response = client.get("/thread-handoff")
    assert response.status_code == 200, response.text
    assert response.json() == {"handoffs": 128, "under_ten_seconds": True}, response.text
    print(response.status_code, response.json())
