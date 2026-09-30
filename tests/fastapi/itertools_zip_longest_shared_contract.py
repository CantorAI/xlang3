from itertools import zip_longest

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get('/zip-longest')
def zip_longest_route() -> dict:
    shared = iter('abcd')
    events = []

    def source():
        events.append('start')
        yield 7

    lazy = zip_longest(source(), [8])
    before = list(events)
    first = next(lazy)
    return {'shared': list(zip_longest(shared, shared)),
            'fill': list(zip_longest([1, 2], [3], fillvalue=9)),
            'before': before, 'first': first, 'after': events}


with TestClient(app) as client:
    response = client.get('/zip-longest')
    print(response.status_code, response.json())
