import csv
import io
import json
import copy

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get('/parse')
def parse() -> list[list[str]]:
    source = io.StringIO('name;note\n\ncafé;"a;b"\n')
    return list(csv.reader(source, dialect='excel', delimiter=';'))


@app.get('/dialect-copy')
def dialect_copy() -> dict[str, str]:
    try:
        copy.copy(csv.get_dialect('excel'))
    except TypeError as exc:
        return {'error': str(exc)}
    return {'error': 'copy unexpectedly succeeded'}


@app.get('/sniff')
def sniff() -> dict[str, object]:
    dialect = csv.Sniffer().sniff("'a''b':c\n'd''e':f\n")
    return {'delimiter': dialect.delimiter, 'quotechar': dialect.quotechar,
            'doublequote': dialect.doublequote}


with TestClient(app) as client:
    response = client.get('/parse')
    print(response.status_code, json.dumps(response.json(), ensure_ascii=True))
    response = client.get('/dialect-copy')
    print(response.status_code, response.json())
    response = client.get('/sniff')
    print(response.status_code, response.json())
