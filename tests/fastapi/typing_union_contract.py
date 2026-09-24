import typing

import truststore
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/typing-union")
def typing_union():
    alias = typing.Callable[[], str | bytes]
    union = str | bytes | alias
    return {
        "alias_member": union.__args__[-1] is alias,
        "literal_member": (union | 42).__args__[-1] == 42,
        "truststore": truststore.__name__,
    }


with TestClient(app) as client:
    response = client.get("/typing-union")
    assert response.status_code == 200, response.text
    assert response.json() == {
        "alias_member": True,
        "literal_member": True,
        "truststore": "truststore",
    }, response.text
    print(response.status_code, response.json())
