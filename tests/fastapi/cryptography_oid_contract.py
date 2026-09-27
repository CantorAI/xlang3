import json

from cryptography.hazmat.bindings._rust import ObjectIdentifier
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/cryptography-oid")
def cryptography_oid() -> dict:
    name_oid = ObjectIdentifier("2.5.4.3")
    alias_oid = ObjectIdentifier("2.5.4.03")
    other_oid = ObjectIdentifier("2.5.4.4")
    try:
        ObjectIdentifier("3.1.2")
    except ValueError as exc:
        invalid_error = str(exc)
    return {
        "canonical": alias_oid.dotted_string,
        "equal": name_oid == alias_oid,
        "set_size": len({name_oid, alias_oid, other_oid}),
        "lookup": {name_oid: "common name"}[alias_oid],
        "name": name_oid._name,
        "representation": repr(name_oid),
        "invalid_error": invalid_error,
    }


response = TestClient(app).get("/cryptography-oid")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
