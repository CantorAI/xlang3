import sys

import uvicorn
from fastapi import FastAPI, Request
from pydantic import BaseModel


class Payload(BaseModel):
    value: int


app = FastAPI()


@app.post("/double")
async def double(payload: Payload, request: Request) -> dict:
    return {"result": payload.value * 2, "scheme": request.url.scheme}


options = {}
if len(sys.argv) == 4:
    options["ssl_certfile"] = sys.argv[2]
    options["ssl_keyfile"] = sys.argv[3]

uvicorn.run(
    app,
    host="127.0.0.1",
    port=int(sys.argv[1]),
    log_level="info",
    **options,
)
