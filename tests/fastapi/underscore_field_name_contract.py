from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, Field


app = FastAPI()


@app.get('/underscore-field-name')
def underscore_field_name():
    try:
        class Invalid(BaseModel):
            ___: int = Field(default=1)
    except NameError as error:
        return {'error': str(error)}


response = TestClient(app).get('/underscore-field-name')
print(response.status_code, response.json())
