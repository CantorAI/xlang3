import warnings
from abc import ABC, abstractmethod

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, root_validator, validator


class Payload(BaseModel, ABC):
    value: int

    with warnings.catch_warnings():
        warnings.simplefilter('ignore')

        @validator('value')
        @classmethod
        @abstractmethod
        def check_value(cls, value):
            return value

    with warnings.catch_warnings():
        warnings.simplefilter('ignore')

        @root_validator(skip_on_failure=True)
        @classmethod
        @abstractmethod
        def check_root(cls, values):
            return values


class Child(Payload):
    pass


app = FastAPI()


@app.get('/abstract')
def abstract_route():
    try:
        Child(value=1)
    except TypeError as error:
        return {'methods': sorted(Child.__abstractmethods__),
                'rejected': 'check_root' in str(error) and 'check_value' in str(error)}
    return {'methods': [], 'rejected': False}


response = TestClient(app).get('/abstract')
print(response.status_code, response.json())
