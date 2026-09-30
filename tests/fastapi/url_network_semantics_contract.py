from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import AnyUrl, HttpUrl, TypeAdapter
from pydantic_core import PydanticSerializationError, SchemaValidator, ValidationError, core_schema


url_validator = SchemaValidator(core_schema.url_schema())
for value in (
    'http://2001:db8::1',
    'http://:password@example.org',
    'http://@example.org',
    'file://localhost/foo/bar',
    'file:////localhost/foo/bar',
    'amqps://',
    'foo:///foo/bar',
):
    try:
        url = url_validator.validate_python(value)
        print(value, str(url), repr(url.username), repr(url.host))
    except ValidationError as error:
        print(value, error.errors(include_url=False)[0]['ctx']['error'])

try:
    TypeAdapter(HttpUrl).dump_python('http://example.com', warnings='error')
except PydanticSerializationError as error:
    print('serializer_error', str(error).splitlines()[0])


app = FastAPI()


@app.get('/url-network-semantics')
def url_network_semantics():
    return {
        'file': str(AnyUrl('file://localhost/foo/bar')),
        'empty_host': str(AnyUrl('amqps://')),
        'password_without_user': AnyUrl('http://:password@example.org').username,
    }


response = TestClient(app).get('/url-network-semantics')
print('http', response.status_code, response.json())
