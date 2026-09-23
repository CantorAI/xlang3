from contextlib import asynccontextmanager
from pathlib import Path
import tempfile
from compression import zstd

from fastapi import FastAPI, WebSocket
from fastapi.responses import FileResponse, Response
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


with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "download.bin"
    content = b"<file content>" * 1000
    path.write_bytes(content)

    @app.get("/file")
    async def download() -> FileResponse:
        return FileResponse(path)

    compressed_content = b"fastapi-zstd-response" * 1000

    @app.get("/zstd")
    async def compressed_response() -> Response:
        return Response(
            content=zstd.compress(compressed_content),
            media_type="application/octet-stream",
            headers={"content-encoding": "zstd"},
        )

    with TestClient(app) as client:
        assert events == ["startup"]
        response = client.post("/double", json={"value": 21})
        assert response.status_code == 200
        assert response.json() == {"result": 42}
        response = client.get("/file")
        assert response.status_code == 200
        assert response.content == content
        response = client.get("/zstd")
        assert response.status_code == 200
        assert response.content == compressed_content
        with client.websocket_connect("/echo") as websocket:
            websocket.send_text("hello")
            assert websocket.receive_text() == "HELLO"

assert events == ["startup", "shutdown"]
print("fastapi-testclient-ok")
