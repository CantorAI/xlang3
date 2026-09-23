from contextlib import asynccontextmanager

from fastapi import FastAPI, WebSocket
from fastapi.testclient import TestClient
from pydantic import BaseModel


events: list[str] = []


class Payload(BaseModel):
    value: int


@asynccontextmanager
async def lifespan(app: FastAPI):
    events.append("startup")
    yield
    events.append("shutdown")


app = FastAPI(lifespan=lifespan)


@app.post("/double")
async def double(payload: Payload) -> dict:
    return {"result": payload.value * 2}


@app.websocket("/echo")
async def echo(websocket: WebSocket) -> None:
    await websocket.accept()
    await websocket.send_text((await websocket.receive_text()).upper())
    await websocket.close()


with TestClient(app) as client:
    assert events == ["startup"]
    response = client.post("/double", json={"value": 21})
    assert response.status_code == 200
    assert response.json() == {"result": 42}
    with client.websocket_connect("/echo") as websocket:
        websocket.send_text("hello")
        assert websocket.receive_text() == "HELLO"

assert events == ["startup", "shutdown"]
print("fastapi-testclient-ok")
