import csv
import io
import array

from fastapi import FastAPI
from fastapi.responses import PlainTextResponse
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/export", response_class=PlainTextResponse)
def export() -> str:
    output = io.StringIO()
    writer = csv.writer(output)
    writer.writerows((("name", "note"), ("café", 'a,b "quoted"')))
    return output.getvalue()


@app.get('/array-export', response_class=PlainTextResponse)
def array_export() -> str:
    output = io.StringIO()
    csv.writer(output).writerow(array.array('w', 'Azé'))
    return output.getvalue()


with TestClient(app) as client:
    response = client.get("/export")
    print(response.status_code, response.headers["content-type"])
    print(repr(response.text))
    response = client.get('/array-export')
    print(response.status_code, repr(response.content))
