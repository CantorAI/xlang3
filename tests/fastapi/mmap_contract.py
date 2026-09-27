from fastapi import FastAPI
from fastapi.testclient import TestClient
import mmap
import tempfile


app = FastAPI()


@app.post("/mmap")
def map_file(payload: str):
    with tempfile.TemporaryFile() as file:
        file.write(payload.encode("ascii"))
        file.flush()
        with mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_COPY) as mapped:
            original = mapped.read().decode("ascii")
            mapped.seek(0)
            mapped.write(b"X")
            mapped.seek(0)
            modified_copy = mapped.read().decode("ascii")
        file.seek(0)
        stored = file.read().decode("ascii")
    return {"original": original, "copy": modified_copy, "stored": stored}


response = TestClient(app).post("/mmap", params={"payload": "alpha"})
print(response.status_code, response.json())
