from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


app = FastAPI()


@app.get('/model-class-keyword')
def model_class_keyword():
    try:
        class Invalid(BaseModel, some_config='new_value'):
            value: int
    except TypeError as error:
        return {'error': str(error)}


response = TestClient(app).get('/model-class-keyword')
print(response.status_code, response.json())
