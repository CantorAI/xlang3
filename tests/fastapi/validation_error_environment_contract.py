import os

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ValidationError


class RequiredValue(BaseModel):
    value: int


app = FastAPI()


@app.get('/validation-error-environment')
def validation_error_environment():
    name = 'PYDANTIC_ERRORS_INCLUDE_URL'
    previous = os.environ.get(name)
    try:
        results = []
        for setting in ('false', 'true'):
            os.environ[name] = setting
            try:
                RequiredValue()
            except ValidationError as error:
                results.append('https://errors.pydantic.dev/' in str(error))
        return results
    finally:
        if previous is None:
            os.environ.pop(name, None)
        else:
            os.environ[name] = previous


response = TestClient(app).get('/validation-error-environment')
print(response.status_code, response.json())
