import os
import tempfile
from pathlib import Path

from fastapi import FastAPI
from fastapi.testclient import TestClient


with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    directory = root / "dist"
    directory.mkdir()
    outside = root / "secret.txt"
    outside.write_text("secret")
    os.symlink(outside, directory / "secret.txt")
    app = FastAPI()
    app.frontend("/", directory=directory)
    with TestClient(app) as client:
        response = client.get("/secret.txt")
        assert response.status_code == 404
        assert response.text != "secret"
        print(response.status_code)
