from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ValidationError


class Payload(BaseModel):
    count: int


app = FastAPI()


@app.get("/float-identity")
def float_identity():
    first = float("nan")
    second = float("nan")
    try:
        Payload(count=first)
    except ValidationError as error:
        actual = error.errors(include_url=False)
    expected = [{
        "type": "finite_number",
        "loc": ("count",),
        "msg": "Input should be a finite number",
        "input": first,
    }]
    return {
        "validation_error_equal": actual == expected,
        "same_nan_equal": {"input": first} == {"input": first},
        "different_nan_equal": {"input": first} == {"input": second},
        "same_object": first is first,
        "different_object": first is second,
    }


response = TestClient(app).get("/float-identity")
print(response.status_code, response.json())
