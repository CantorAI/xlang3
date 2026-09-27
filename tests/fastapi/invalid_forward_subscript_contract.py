from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


app = FastAPI()


@app.get("/invalid-forward-subscript")
def invalid_forward_subscript():
    class Plain:
        pass

    try:
        class Payload(BaseModel):
            value: "Plain[int]"
    except Exception as error:
        return {"error": type(error).__name__, "message": str(error)}
    return {"error": "none"}


response = TestClient(app).get("/invalid-forward-subscript")
print(response.status_code, response.json())
