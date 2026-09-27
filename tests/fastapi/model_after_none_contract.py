import warnings

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, model_validator


class Payload(BaseModel):
    value: int = 1

    @model_validator(mode="after")
    def return_none(self):
        return None


app = FastAPI()


@app.get("/model-after-none")
def model_after_none():
    with warnings.catch_warnings(record=True) as captured:
        warnings.simplefilter("always")
        item = Payload(value=2)
    return {
        "dump": item.model_dump(),
        "warning_count": len(captured),
        "warning": str(captured[0].message).splitlines()[0],
    }


response = TestClient(app).get("/model-after-none")
print(response.status_code, response.json())
