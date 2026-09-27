import re

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, Field
from pydantic_core import SchemaError, SchemaValidator


pattern = r'^(\p{L}|_)(\p{L}|\p{N}|[.\-_])*$'
validator = SchemaValidator({'type': 'str', 'pattern': pattern})
for value in ('Alice_42', 'É中9', '1Alice', 'A!'):
    try:
        validator.validate_python(value)
        print('rust', True)
    except ValueError:
        print('rust', False)

python_pattern = SchemaValidator({'type': 'str', 'pattern': re.compile(r'^[A-Z]+$')})
print('compiled', python_pattern.validate_python('ABC'))
try:
    SchemaValidator({'type': 'str', 'pattern': pattern}, {'regex_engine': 'python-re'})
except SchemaError:
    print('python-re-rejects-rust-syntax')


class Name(BaseModel):
    value: str = Field(pattern=pattern)


app = FastAPI()


@app.post('/names')
def names(name: Name):
    return {'value': name.value}


client = TestClient(app)
for value in ('É中9', '1Alice'):
    response = client.post('/names', json={'value': value})
    print('http', response.status_code)
