import signal

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/signal-worker")
def signal_worker():
    errors = []
    for operation in (
        lambda: signal.signal(signal.SIGINT, signal.getsignal(signal.SIGINT)),
        lambda: signal.set_wakeup_fd(-1),
    ):
        try:
            operation()
        except Exception as exc:
            errors.append(type(exc).__name__)
        else:
            errors.append("accepted")
    return {"errors": errors}


with TestClient(app) as client:
    response = client.get("/signal-worker")
    assert response.status_code == 200
    assert response.json() == {"errors": ["ValueError", "ValueError"]}
    print(response.json())
