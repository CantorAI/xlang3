import sys

import uvicorn
from fastapi import FastAPI, WebSocket


app = FastAPI()


@app.websocket("/echo")
async def echo(websocket: WebSocket):
    await websocket.accept()
    message = await websocket.receive_text()
    await websocket.send_text(message.upper())
    await websocket.close(code=1000)


if __name__ == "__main__":
    uvicorn.run(app, host="127.0.0.1", port=int(sys.argv[1]),
                loop="asyncio", http="h11", ws="wsproto", log_level="warning")
