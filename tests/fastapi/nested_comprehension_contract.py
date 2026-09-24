from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/xor-table/{size}")
def xor_table(size: int):
    return {"rows": [list(bytes(a ^ b for a in range(size))) for b in range(size)]}


with TestClient(app) as client:
    response = client.get("/xor-table/4")
    assert response.status_code == 200
    assert response.json() == {
        "rows": [[0, 1, 2, 3], [1, 0, 3, 2], [2, 3, 0, 1], [3, 2, 1, 0]]
    }
    print(response.json())
