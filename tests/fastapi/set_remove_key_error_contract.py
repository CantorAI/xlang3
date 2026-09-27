from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/set-remove-key-error")
def set_remove_key_error():
    values = {1, 2}
    try:
        values.remove("absent")
    except KeyError as exc:
        return {
            "error": type(exc).__name__,
            "args": list(exc.args),
            "size": len(values),
        }


response = TestClient(app).get("/set-remove-key-error")
print(response.status_code, response.json())
