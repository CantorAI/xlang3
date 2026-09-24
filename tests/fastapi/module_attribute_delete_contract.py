import email.utils
from unittest.mock import mock_open, patch

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/module-patches")
def module_patches():
    reads = []
    for content in ("first", "second"):
        with patch("email.utils.open", mock_open(read_data=content)):
            reads.append(email.utils.open().read())
        assert "open" not in email.utils.__dict__
    return {"reads": reads}


with TestClient(app) as client:
    response = client.get("/module-patches")
    assert response.status_code == 200
    assert response.json() == {"reads": ["first", "second"]}
    print(response.json())
