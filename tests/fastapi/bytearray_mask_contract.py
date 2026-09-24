from fastapi import FastAPI, WebSocket
from fastapi.testclient import TestClient


app = FastAPI()


@app.websocket("/mask")
async def mask_bytes(websocket: WebSocket):
    await websocket.accept()
    payload = await websocket.receive_bytes()
    mask = bytearray(b"\x01\x02\x03\x04")
    repeated = mask * (len(payload) // 4) + mask[: len(payload) % 4]
    await websocket.send_bytes(bytes(left ^ right for left, right in zip(payload, repeated)))


with TestClient(app) as client:
    with client.websocket_connect("/mask") as websocket:
        websocket.send_bytes(b"hello")
        result = websocket.receive_bytes()
        assert result == b"igohn", result
        print(result)
