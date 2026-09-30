import hashlib

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


class WheelLike:
    algorithm = hashlib.sha256

    def digest(self, data):
        return self.algorithm(data).hexdigest()


@app.get('/wheel-hash')
def wheel_hash() -> dict:
    return {'sha256': WheelLike().digest(b'wheel-record')}


with TestClient(app) as client:
    response = client.get('/wheel-hash')
    print(response.status_code, response.json())
