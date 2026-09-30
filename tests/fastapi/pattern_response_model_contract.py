import re

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, Field


class Payload(BaseModel):
    pattern: re.Pattern[str]


app = FastAPI()


@app.get('/pattern', response_model=Payload)
def pattern_route():
    return {'pattern': '^a+$'}


response = TestClient(app).get('/pattern')
print(response.status_code, response.json())
pattern = Payload(pattern='^a+$').pattern
print(type(pattern).__module__, type(pattern).__name__)
match = pattern.match('aaa')
print(type(match).__module__, type(match).__name__, repr(match))


class PatternPayload(BaseModel):
    text: str = Field(pattern=re.compile(r'^whatev.r\d$'))


@app.post('/pattern-check')
def pattern_check(payload: PatternPayload):
    return {'text': payload.text}


error_response = TestClient(app).post('/pattern-check', json={'text': ' whatever1'})
error = error_response.json()['detail'][0]
print(error_response.status_code, error['msg'], error['ctx'])
