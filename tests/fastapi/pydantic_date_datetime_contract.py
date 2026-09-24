from datetime import date

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel
from pydantic_core import SchemaValidator, ValidationError, core_schema


validator = SchemaValidator(core_schema.date_schema())
for text in (
    "2000-01-01 00:00:00",
    "2000-01-01T00:00:00",
    "2000-01-01 00:00:00.000000",
    "2000-01-01 00:00:00Z",
    "2000-01-01 00:00:00+01:00",
):
    assert validator.validate_python(text) == date(2000, 1, 1)
    assert validator.validate_json('"' + text + '"') == date(2000, 1, 1)
print("midnight", date(2000, 1, 1))

for text in ("2000-01-01 00:00:01", "2000-01-01 00:00:00.000001"):
    try:
        validator.validate_python(text)
    except ValidationError as exc:
        assert exc.errors()[0]["type"] == "date_from_datetime_inexact"
    else:
        raise AssertionError("non-midnight datetime was accepted")
print("non-midnight", "date_from_datetime_inexact")


class Event(BaseModel):
    day: date


app = FastAPI()


@app.post("/events")
def create_event(event: Event) -> Event:
    return event


with TestClient(app) as client:
    accepted = client.post("/events", json={"day": "2000-01-01 00:00:00"})
    assert accepted.status_code == 200, accepted.text
    print("http", accepted.status_code, accepted.json())

    rejected = client.post("/events", json={"day": "2000-01-01 00:00:01"})
    assert rejected.status_code == 422, rejected.text
    detail = rejected.json()["detail"][0]
    print("http", rejected.status_code, detail["type"], detail["loc"])
