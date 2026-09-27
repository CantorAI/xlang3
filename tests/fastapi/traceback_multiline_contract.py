import traceback

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/traceback")
def traceback_route():
    return int(
        "invalid",
    )


@app.exception_handler(ValueError)
async def value_error_handler(request: Request, exc: ValueError):
    rendered = "".join(traceback.format_exception(exc))
    return JSONResponse({"type": type(exc).__name__, "route": "traceback_route" in rendered,
                         "message": "invalid" in rendered})


response = TestClient(app).get("/traceback")
print(response.status_code, response.json())
