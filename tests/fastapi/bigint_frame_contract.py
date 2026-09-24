import sys

from fastapi import FastAPI, WebSocket
from fastapi.testclient import TestClient


app = FastAPI()


@app.websocket("/large-mask")
async def large_mask(websocket: WebSocket):
    await websocket.accept()
    payload = await websocket.receive_bytes()
    mask = bytearray(b"\x01\x02\x03\x04")
    repeated = mask * (len(payload) // 4) + mask[: len(payload) % 4]
    data_int = int.from_bytes(payload, sys.byteorder)
    mask_int = int.from_bytes(repeated, sys.byteorder)
    result = (data_int ^ mask_int).to_bytes(len(payload), sys.byteorder)
    await websocket.send_bytes(result)


with TestClient(app) as client:
    with client.websocket_connect("/large-mask") as websocket:
        websocket.send_bytes(b"\x01" * 1048576)
        result = websocket.receive_bytes()
        assert len(result) == 1048576
        assert result[:4] == b"\x00\x03\x02\x05"
        assert result[-4:] == b"\x00\x03\x02\x05"
        print(len(result), result[:4], result[-4:])
