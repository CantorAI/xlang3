from collections import deque

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get('/deque', response_model=deque[str])
def deque_route():
    return deque(['a', 'b'])


response = TestClient(app).get('/deque')
print(response.status_code, response.json())
