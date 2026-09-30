from fastapi import Body, FastAPI
from fastapi.responses import JSONResponse
from fastapi.testclient import TestClient
from pydantic_core import SchemaValidator, ValidationError, core_schema


validators = {
    'url': SchemaValidator(core_schema.url_schema()),
    'multi-host-url': SchemaValidator(core_schema.multi_host_url_schema()),
}


def parse(kind, value):
    try:
        return str(validators[kind].validate_python(value))
    except ValidationError as error:
        return error.errors(include_url=False)[0]['ctx']['error']


for kind in validators:
    for value in ('', ' ', '\t', '\n', ' \x00 ', ' https://example.org '):
        print(kind, repr(value), parse(kind, value))


app = FastAPI()


@app.post('/blank/{kind}')
def blank_url(kind: str, value: str = Body(...)):
    result = parse(kind, value)
    if result in ('input is empty', 'relative URL without a base'):
        return JSONResponse(status_code=422, content={'error': result})
    return {'url': result}


with TestClient(app) as client:
    for value in ('', ' ', ' https://example.org '):
        response = client.post('/blank/url', json=value)
        print('http', repr(value), response.status_code, response.json())
