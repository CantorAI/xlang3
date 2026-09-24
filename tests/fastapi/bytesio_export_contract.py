import gc
import io
import sys

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/buffer-export")
def buffer_export():
    stream = io.BytesIO(b"fastapi")
    view = stream.getbuffer()
    child = memoryview(view)
    view.release()
    del stream
    gc.collect()
    result = bytes(child).decode("ascii")
    child.release()
    return {"data": result}


unraisable = []
previous_hook = sys.unraisablehook
sys.unraisablehook = lambda args: unraisable.append(type(args.exc_value).__name__)
try:
    with TestClient(app) as client:
        response = client.get("/buffer-export")
        assert response.status_code == 200
        assert response.json() == {"data": "fastapi"}
        gc.collect()
        assert unraisable == [], unraisable
        print(response.json())
finally:
    sys.unraisablehook = previous_hook
