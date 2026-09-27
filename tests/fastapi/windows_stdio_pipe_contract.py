"""CPython oracle for Windows standard-output newlines and pipe errors."""

import json
import subprocess
import sys

from fastapi import FastAPI
from fastapi.testclient import TestClient


def check_runtime():
    text = subprocess.run(
        [sys.executable, "-c", "print('line')"],
        check=True,
        capture_output=True,
    ).stdout
    binary = subprocess.run(
        [sys.executable, "-c", "import sys; sys.stdout.buffer.write(b'line\\n')"],
        check=True,
        capture_output=True,
    ).stdout
    pipe_error = OSError(0, "The pipe is being closed", None, 232)
    return {
        "text_crlf": text == b"line\r\n",
        "binary_lf": binary == b"line\n",
        "pipe_type": type(pipe_error).__name__,
        "pipe_errno": pipe_error.errno,
        "pipe_winerror": pipe_error.winerror,
    }


print("runtime", json.dumps(check_runtime(), sort_keys=True))

app = FastAPI()


@app.get("/windows-stdio-pipe")
def windows_stdio_pipe():
    return check_runtime()


response = TestClient(app).get("/windows-stdio-pipe")
print("fastapi", response.status_code, json.dumps(response.json(), sort_keys=True))
