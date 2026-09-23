from datetime import datetime, timezone
from typing import Annotated

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ConfigDict, PlainSerializer, computed_field, field_serializer


class ArrayValue:
    def __init__(self, values):
        self.values = values


def serialize_array(value):
    return value.values


ArrayField = Annotated[ArrayValue, PlainSerializer(serialize_array)]


class Report(BaseModel):
    model_config = ConfigDict(arbitrary_types_allowed=True)
    width: int
    length: int
    created: datetime
    samples: ArrayField

    @computed_field
    @property
    def area(self) -> int:
        return self.width * self.length

    @field_serializer("created")
    def serialize_created(self, value: datetime):
        return value.replace(microsecond=0, tzinfo=timezone.utc).isoformat()


app = FastAPI()


@app.get("/report", response_model=Report)
def get_report():
    return Report(
        width=3,
        length=4,
        created=datetime(2019, 1, 1, 8),
        samples=ArrayValue([1.0, 2.0, 3.0]),
    )


with TestClient(app) as client:
    response = client.get("/report")
    assert response.status_code == 200, response.text
    assert response.json() == {
        "width": 3,
        "length": 4,
        "created": "2019-01-01T08:00:00+00:00",
        "samples": [1.0, 2.0, 3.0],
        "area": 12,
    }

print("fastapi-pydantic-serializers-ok")
