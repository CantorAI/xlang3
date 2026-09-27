import warnings

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic.config import Extra
from pydantic.warnings import PydanticDeprecatedSince20

app = FastAPI()


@app.get('/extra')
def extra():
    return {'mode': Extra.allow}


with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter('always', PydanticDeprecatedSince20)
    response = TestClient(app).get('/extra')

print('response', response.status_code, response.json())
print('warnings', [(type(item.message).__name__, str(item.message)) for item in caught])
