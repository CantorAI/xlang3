from typing import Annotated

from annotated_types import MaxLen
from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


def make_app():
    def make_model():
        class Payload(BaseModel):
            entries: Annotated[List[Dict[str, str]], MaxLen(1)]

        Dict = dict
        return Payload

    List = list
    Payload = make_model()
    app = FastAPI()

    @app.post('/entries')
    def entries(payload: Payload):
        return {'count': len(payload.entries)}

    return app


client = TestClient(make_app())
valid = client.post('/entries', json={'entries': [{'a': 'b'}]})
invalid = client.post('/entries', json={'entries': [{'a': 'b'}, {'c': 'd'}]})
print('valid', valid.status_code, valid.json())
print('invalid', invalid.status_code, invalid.json()['detail'][0]['type'])
