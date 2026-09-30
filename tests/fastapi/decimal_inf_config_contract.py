from decimal import Decimal

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ConfigDict, ValidationError


class Allowed(BaseModel):
    model_config = ConfigDict(allow_inf_nan=True)
    value: Decimal


class Forbidden(BaseModel):
    model_config = ConfigDict(allow_inf_nan=False)
    value: Decimal


print('allowed', Allowed(value=Decimal('Inf')).value.is_infinite())
try:
    Forbidden(value=Decimal('Inf'))
except ValidationError as exc:
    print('forbidden', exc.errors()[0]['type'])

app = FastAPI()


@app.post('/decimal')
def decimal(body: Allowed):
    return {'infinite': body.value.is_infinite()}


client = TestClient(app)
print('http', client.post('/decimal', json={'value': 'Inf'}).json())
