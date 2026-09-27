import os
import subprocess
import sys
import tempfile

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get('/child-cwd')
def child_cwd() -> dict:
    original = os.getcwd()
    with tempfile.TemporaryDirectory() as directory:
        os.chdir(directory)
        try:
            child = subprocess.check_output(
                [sys.executable, '-c', 'import os; print(os.getcwd())'], text=True
            ).strip()
            inherited = os.path.normcase(os.path.abspath(child)) == os.path.normcase(
                os.path.abspath(directory)
            )
        finally:
            os.chdir(original)
    return {'inherited': inherited}


with TestClient(app) as client:
    response = client.get('/child-cwd')
    print(response.status_code, response.json())
