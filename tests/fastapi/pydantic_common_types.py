from datetime import date, datetime, time, timedelta
from decimal import Decimal
from typing import Annotated
from uuid import UUID

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, Field, ValidationError


class CommonTypes(BaseModel):
    raw: bytes
    tags: set[int]
    immutable_tags: frozenset[int]
    point: tuple[int, str]
    trail: tuple[int, ...]
    short: Annotated[str, Field(min_length=2, max_length=5)]
    created: datetime
    day: date
    clock: time
    duration: timedelta
    identifier: UUID
    amount: Decimal


app = FastAPI()


@app.post("/common", response_model=CommonTypes)
def common_types(value: CommonTypes) -> CommonTypes:
    return value


with TestClient(app) as client:
    response = client.post(
        "/common",
        json={
            "raw": "hello",
            "tags": [3, "2", 3],
            "immutable_tags": [4, "5", 4],
            "point": [7, "north"],
            "trail": [1, "2", 3],
            "short": "valid",
            "created": "2026-09-19T12:34:56.123456",
            "day": "2026-09-19",
            "clock": "12:34:56.123456",
            "duration": "P1DT2H3M4.5S",
            "identifier": "12345678-1234-5678-1234-567812345678",
            "amount": "12.340",
        },
    )
    assert response.status_code == 200, response.text
    payload = response.json()
    assert payload["raw"] == "hello"
    assert set(payload["tags"]) == {2, 3}
    assert set(payload["immutable_tags"]) == {4, 5}
    assert payload["point"] == [7, "north"]
    assert payload["trail"] == [1, 2, 3]
    assert payload["short"] == "valid"
    assert payload["created"] == "2026-09-19T12:34:56.123456"
    assert payload["day"] == "2026-09-19"
    assert payload["clock"] == "12:34:56.123456"
    assert payload["duration"] == "P1DT2H3M4.5S"
    assert payload["identifier"] == "12345678-1234-5678-1234-567812345678"
    assert payload["amount"] == "12.340"

model = CommonTypes.model_validate({
    "raw": "data",
    "tags": (1, "2", 1),
    "immutable_tags": [9, "10", 9],
    "point": (4, "west"),
    "trail": [5, "6", 7, "8"],
    "short": "okay",
    "created": "2026-09-20T01:02:03",
    "day": "2026-09-20",
    "clock": "01:02:03",
    "duration": 90.5,
    "identifier": "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
    "amount": "99.50",
})
assert model.raw == b"data"
assert model.tags == {1, 2}
assert model.immutable_tags == frozenset({9, 10})
assert model.point == (4, "west")
assert model.trail == (5, 6, 7, 8)
assert model.created == datetime(2026, 9, 20, 1, 2, 3)
assert model.day == date(2026, 9, 20)
assert model.clock == time(1, 2, 3)
assert model.duration == timedelta(seconds=90.5)
assert model.identifier == UUID("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
assert model.amount == Decimal("99.50")

try:
    CommonTypes.model_validate(
        {
            "raw": "data",
            "tags": [],
            "immutable_tags": [],
            "point": [1, "x"],
            "trail": [],
            "short": "x",
            "created": "2026-09-19T12:00:00",
            "day": "2026-09-19",
            "clock": "12:00:00",
            "duration": "PT1M",
            "identifier": "12345678-1234-5678-1234-567812345678",
            "amount": "1.0",
        }
    )
except ValidationError as error:
    detail = error.errors()[0]
    assert detail["type"] == "string_too_short"
    assert detail["loc"] == ("short",)
else:
    raise AssertionError("short strings must fail validation")

print("fastapi-pydantic-common-types-ok")
