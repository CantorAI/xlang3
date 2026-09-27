import gc
import warnings

from anyio import create_memory_object_stream
from fastapi import FastAPI
from fastapi.testclient import TestClient


events = []


class Finalized:
    def __del__(self):
        events.append("deleted")


def release_instance():
    value = Finalized()
    del value
    gc.collect()


release_instance()

app = FastAPI()


@app.get("/finalization")
async def finalization():
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always", ResourceWarning)
        send, receive = create_memory_object_stream[int]()
        del send
        gc.collect()
        del receive
        gc.collect()
        return {"warnings": [type(item.message).__name__ for item in caught]}


with TestClient(app) as client:
    response = client.get("/finalization")
    print(response.status_code)
    print(response.json())
print(events)
