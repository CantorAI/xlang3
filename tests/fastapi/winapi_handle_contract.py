import subprocess
import sys

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/handles")
def handles():
    import _winapi

    read_handle, write_handle = _winapi.CreatePipe(None, 0)
    duplicate = _winapi.DuplicateHandle(
        _winapi.GetCurrentProcess(),
        read_handle,
        _winapi.GetCurrentProcess(),
        0,
        True,
        _winapi.DUPLICATE_SAME_ACCESS,
    )
    try:
        source_open = _winapi.GetFileType(read_handle) == _winapi.FILE_TYPE_PIPE
    finally:
        _winapi.CloseHandle(duplicate)
        _winapi.CloseHandle(read_handle)
        _winapi.CloseHandle(write_handle)

    child = subprocess.run(
        [sys.executable, "-c", "print('child-ok')"],
        capture_output=True,
        text=True,
        check=True,
    )
    return {"source_open": source_open, "child": child.stdout.strip()}


with TestClient(app) as client:
    response = client.get("/handles")
    print(response.status_code)
    print(response.json())
