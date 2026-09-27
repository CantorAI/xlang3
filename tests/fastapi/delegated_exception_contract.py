import json

from fastapi import FastAPI, HTTPException
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/ok")
async def ok():
    return {"ok": True}


@app.get("/denied")
def denied():
    raise HTTPException(status_code=401, detail="Denied")


client = TestClient(app)
responses = [client.get("/ok"), client.get("/denied")]
print(json.dumps([[response.status_code, response.json()] for response in responses]))
