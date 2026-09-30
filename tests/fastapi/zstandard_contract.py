from fastapi import FastAPI
from fastapi.testclient import TestClient
import zstandard


app = FastAPI()


@app.post("/zstandard")
def round_trip(payload: bytes):
    compressed = zstandard.compress(payload)
    restored = zstandard.decompress(compressed)
    return {
        "backend": zstandard.backend,
        "frame": compressed.hex(),
        "restored": restored.decode("ascii"),
        "version": zstandard.__version__,
    }


response = TestClient(app).post("/zstandard", params={"payload": "foo"})
print(response.status_code, response.json())

try:
    zstandard.decompress(b"not a frame")
except zstandard.ZstdError as exc:
    print(type(exc).__name__, str(exc))
