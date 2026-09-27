from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ConfigDict


class Payload(BaseModel):
    value: int
    model_config = ConfigDict(validate_assignment=True, extra="allow")


app = FastAPI()


@app.get("/extra-assignment")
def extra_assignment():
    item = Payload(value=2)
    item.extra_field = 1.23
    item.extra_field = "ready"
    return {
        "extra": item.extra_field,
        "dump": item.model_dump(),
        "fields_set": sorted(item.model_fields_set),
    }


response = TestClient(app).get("/extra-assignment")
print(response.status_code, response.json())
