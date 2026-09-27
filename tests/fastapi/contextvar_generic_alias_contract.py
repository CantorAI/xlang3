from contextvars import ContextVar, Token

from fastapi import FastAPI
from fastapi.testclient import TestClient


request_number = ContextVar[int]("request_number", default=0)
app = FastAPI()


@app.get("/contextvar-generic-alias")
def contextvar_generic_alias():
    token = request_number.set(29)
    try:
        return {
            "value": request_number.get(),
            "contextvar_origin": ContextVar[int].__origin__ is ContextVar,
            "token_origin": Token[int].__origin__ is Token,
        }
    finally:
        request_number.reset(token)


response = TestClient(app).get("/contextvar-generic-alias")
print(response.status_code, response.json())
