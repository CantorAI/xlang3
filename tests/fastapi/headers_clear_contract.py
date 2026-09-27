from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/headers")
def headers():
    return {"ok": True}


with TestClient(app) as client:
    client.headers.clear()
    response = client.get("/headers")
    assert response.status_code == 200
    assert response.json() == {"ok": True}
    print(response.status_code, response.json())
