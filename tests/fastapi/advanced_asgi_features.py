import asyncio
from contextlib import asynccontextmanager

from fastapi import BackgroundTasks, FastAPI, WebSocket
from fastapi.responses import StreamingResponse


events: list[str] = []


@asynccontextmanager
async def lifespan(app: FastAPI):
    events.append("startup")
    yield
    events.append("shutdown")


app = FastAPI(lifespan=lifespan)


async def chunks():
    yield b"hello,"
    await asyncio.sleep(0)
    yield b" xlang3"


@app.get("/stream")
async def stream() -> StreamingResponse:
    return StreamingResponse(chunks(), media_type="text/plain")


def record_background(value: str) -> None:
    events.append(value)


@app.post("/background")
async def background(tasks: BackgroundTasks) -> dict:
    tasks.add_task(record_background, "background")
    return {"scheduled": True}


@app.websocket("/echo")
async def echo(websocket: WebSocket) -> None:
    await websocket.accept()
    value = await websocket.receive_text()
    await websocket.send_text(value.upper())
    await websocket.close(code=1000)


async def run_lifespan() -> list[dict]:
    inputs = [
        {"type": "lifespan.startup"},
        {"type": "lifespan.shutdown"},
    ]
    outputs: list[dict] = []

    async def receive() -> dict:
        return inputs.pop(0)

    async def send(message: dict) -> None:
        outputs.append(message)

    await app({"type": "lifespan", "asgi": {"version": "3.0", "spec_version": "2.0"}, "state": {}}, receive, send)
    return outputs


async def request(method: str, path: str) -> tuple[int, bytes, list[dict]]:
    sent = False
    outputs: list[dict] = []

    async def receive() -> dict:
        nonlocal sent
        if sent:
            return {"type": "http.disconnect"}
        sent = True
        return {"type": "http.request", "body": b"", "more_body": False}

    async def send(message: dict) -> None:
        outputs.append(message)

    scope = {
        "type": "http",
        "asgi": {"version": "3.0", "spec_version": "2.4"},
        "http_version": "1.1",
        "method": method,
        "scheme": "http",
        "path": path,
        "raw_path": path.encode("ascii"),
        "query_string": b"",
        "root_path": "",
        "headers": [(b"host", b"testserver")],
        "client": ("127.0.0.1", 50000),
        "server": ("testserver", 80),
        "state": {},
    }
    await app(scope, receive, send)
    start = next(message for message in outputs if message["type"] == "http.response.start")
    body = b"".join(
        message.get("body", b"") for message in outputs if message["type"] == "http.response.body"
    )
    return start["status"], body, outputs


async def run_websocket() -> list[dict]:
    inputs = [
        {"type": "websocket.connect"},
        {"type": "websocket.receive", "text": "hello"},
    ]
    outputs: list[dict] = []

    async def receive() -> dict:
        return inputs.pop(0)

    async def send(message: dict) -> None:
        outputs.append(message)

    scope = {
        "type": "websocket",
        "asgi": {"version": "3.0", "spec_version": "2.3"},
        "http_version": "1.1",
        "scheme": "ws",
        "path": "/echo",
        "raw_path": b"/echo",
        "query_string": b"",
        "root_path": "",
        "headers": [(b"host", b"testserver")],
        "client": ("127.0.0.1", 50000),
        "server": ("testserver", 80),
        "subprotocols": [],
        "state": {},
    }
    await app(scope, receive, send)
    return outputs


async def main() -> None:
    schema = app.openapi()
    assert schema["openapi"].startswith("3.")
    assert schema["paths"]["/stream"]["get"]["responses"]["200"]

    lifespan_messages = await run_lifespan()
    assert [message["type"] for message in lifespan_messages] == [
        "lifespan.startup.complete",
        "lifespan.shutdown.complete",
    ]
    assert events[:2] == ["startup", "shutdown"]

    status, body, stream_messages = await request("GET", "/stream")
    assert status == 200
    assert body == b"hello, xlang3"
    body_messages = [message for message in stream_messages if message["type"] == "http.response.body"]
    assert len(body_messages) == 3
    assert body_messages[-1].get("more_body", False) is False

    status, body, _ = await request("POST", "/background")
    assert status == 200
    assert body == b'{"scheduled":true}'
    assert events[-1] == "background"

    websocket_messages = await run_websocket()
    assert websocket_messages == [
        {"type": "websocket.accept", "subprotocol": None, "headers": []},
        {"type": "websocket.send", "text": "HELLO"},
        {"type": "websocket.close", "code": 1000, "reason": ""},
    ]
    print("fastapi-advanced-asgi-ok")


asyncio.run(main())
