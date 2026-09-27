import warnings

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, root_validator


with warnings.catch_warnings():
    warnings.simplefilter("ignore", DeprecationWarning)

    class Payload(BaseModel):
        value: int = 1

        @root_validator(skip_on_failure=True)
        def discard_values(cls, values):
            return None


app = FastAPI()


@app.get("/invalid-validator")
def invalid_validator():
    try:
        Payload()
    except Exception as error:
        return {"type": type(error).__name__, "message": str(error)}


response = TestClient(app).get("/invalid-validator")
print(response.status_code, response.json())
