from enum import IntEnum

from fastapi import FastAPI
from fastapi.testclient import TestClient


class TrailType(IntEnum):
    STOP = 1
    CHOICE = 3


assert 7 - TrailType.CHOICE - 1 == 3
assert TrailType.CHOICE - TrailType.STOP == 2
print("arithmetic", 7 - TrailType.CHOICE - 1)


app = FastAPI()


@app.get("/span/{record}")
def span(record: int) -> dict[str, int]:
    return {
        "label_index": record - TrailType.CHOICE - 1,
        "stop_distance": TrailType.CHOICE - TrailType.STOP,
    }


with TestClient(app) as client:
    accepted = client.get("/span/7")
    assert accepted.status_code == 200, accepted.text
    print("http", accepted.status_code, accepted.json())
