from datetime import timedelta

from pydantic import TypeAdapter
from fastapi import FastAPI
from fastapi.testclient import TestClient


adapter = TypeAdapter(timedelta)
for value in ('4d,00:15:30', '-4d,00:15:30', '4 days, 00:15:30'):
    print(adapter.validate_python(value))

app = FastAPI()


@app.get('/duration')
def duration(value: timedelta):
    return {'seconds': value.total_seconds()}


client = TestClient(app)
for value in ('1 days 10:10', '1 d 10:10', '1 10:10'):
    response = client.get('/duration', params={'value': value})
    if response.status_code == 200:
        print(value, response.status_code, response.json())
    else:
        print(value, response.status_code)
response = client.get('/duration', params={'value': '15:30.0001broken'})
print('trailing', response.status_code,
      [(entry['type'], entry['msg'], entry.get('ctx'))
       for entry in response.json()['detail']])
