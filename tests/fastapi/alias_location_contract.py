from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ConfigDict, Field, ValidationError


class Person(BaseModel):
    model_config = ConfigDict(loc_by_alias=False)
    age: int = Field(alias='_age')


try:
    Person(_age='bad')
except ValidationError as error:
    print('model', error.errors(include_url=False)[0]['loc'])


app = FastAPI()


@app.post('/people')
def people(person: Person):
    return {'age': person.age}


client = TestClient(app)
valid = client.post('/people', json={'_age': '12'})
invalid = client.post('/people', json={'_age': 'bad'})
print('valid', valid.status_code, valid.json())
print('invalid', invalid.status_code, invalid.json()['detail'][0]['loc'])


class Editor(BaseModel):
    model_config = ConfigDict(extra='forbid', validate_by_name=True)
    last_updated_by: str | None = Field(None, alias='lastUpdatedBy')


print('by-alias', Editor(lastUpdatedBy='Ada').last_updated_by)
print('by-name', Editor(last_updated_by='Ada').last_updated_by)
try:
    Editor(lastUpdatedBy='Ada', last_updated_by='Grace')
except ValidationError as error:
    print('duplicate', error.errors(include_url=False)[0]['loc'])


@app.post('/editors')
def editors(editor: Editor):
    return {'name': editor.last_updated_by}


duplicate = client.post('/editors', json={
    'lastUpdatedBy': 'Ada', 'last_updated_by': 'Grace'
})
print('duplicate-http', duplicate.status_code,
      duplicate.json()['detail'][0]['loc'])
