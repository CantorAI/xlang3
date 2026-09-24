from datetime import datetime, timedelta, timezone
from typing import Any

from fastapi import Body, FastAPI
from fastapi.testclient import TestClient
from pydantic_core import TzInfo


class ModuloProbe:
    def __mod__(self, other):
        return ("forward", other)


class ReflectedModuloProbe:
    def __rmod__(self, other):
        return ("reflected", other)


assert ModuloProbe() % 7 == ("forward", 7)
assert 7 % ReflectedModuloProbe() == ("reflected", 7)
hour = timedelta(hours=1)
minute = timedelta(minutes=1)
assert hour % minute == timedelta(0)
assert timedelta(seconds=61) % minute == timedelta(seconds=1)
assert TzInfo(0) < TzInfo(3600)
assert TzInfo(3600) > timezone.utc
assert TzInfo.__lt__(TzInfo(0), object()) is NotImplemented
zone = TzInfo(3600)
assert zone.fromutc(datetime(2026, 1, 1, tzinfo=zone)).isoformat() == \
    "2026-01-01T01:00:00+01:00"
print("native", zone.fromutc(datetime(2026, 1, 1, tzinfo=zone)).isoformat())


app = FastAPI()


@app.post("/timezone")
def timezone_shift(payload: dict[str, Any] = Body(...)):
    zone = TzInfo(payload["offset_seconds"])
    utc = datetime.fromisoformat(payload["utc"])
    local = zone.fromutc(utc.replace(tzinfo=zone))
    return {"local": local.isoformat(), "remainder": (
        timedelta(seconds=payload["offset_seconds"]) % minute
    ).total_seconds()}


with TestClient(app) as client:
    response = client.post(
        "/timezone", json={"offset_seconds": 3661, "utc": "2026-01-01T00:00:00"}
    )
    assert response.status_code == 200, response.text
    assert response.json() == {
        "local": "2026-01-01T01:01:01+01:01:01", "remainder": 1.0
    }
    print("http", response.status_code, response.json())
