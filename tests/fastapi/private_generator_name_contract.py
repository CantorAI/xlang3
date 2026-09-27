from fastapi import FastAPI
from fastapi.testclient import TestClient


class Snapshot:
    def __init__(self):
        self.__preserve = lambda value: value % 2 == 0

    def restore(self):
        return list(value for value in range(5) if self.__preserve(value))


app = FastAPI()


@app.get('/restore')
def restore() -> dict:
    return {'accepted': Snapshot().restore()}


with TestClient(app) as client:
    response = client.get('/restore')
    print(response.status_code, response.json())
