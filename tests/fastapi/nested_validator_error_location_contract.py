from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ValidationError
from pydantic_core import core_schema


class Wrapped:
    @classmethod
    def __get_pydantic_core_schema__(cls, source, handler):
        def validate(value, info):
            raise ValidationError.from_exception_data(
                'Wrapped',
                [{'type': 'int_parsing', 'loc': ('inner',), 'input': 'bad'}],
            )

        return core_schema.with_info_after_validator_function(
            validate, core_schema.any_schema())


class Payload(BaseModel):
    nested: Wrapped


app = FastAPI()


@app.post('/validate')
def validate_route(payload: Payload):
    return {'ok': True}


response = TestClient(app).post('/validate', json={'nested': 'bad'})
detail = response.json()['detail'][0]
print(response.status_code, detail['type'], detail['loc'])
