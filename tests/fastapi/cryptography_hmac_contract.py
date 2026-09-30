import json

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, hmac
from fastapi import FastAPI, Header, HTTPException
from fastapi.testclient import TestClient


app = FastAPI()
key = b"shared-production-secret"


def signature(message: bytes) -> str:
    context = hmac.HMAC(key, hashes.SHA256())
    context.update(message)
    return context.finalize().hex()


@app.get("/authenticated")
def authenticated(x_signature: str = Header()) -> dict:
    context = hmac.HMAC(key, hashes.SHA256())
    context.update(b"GET /authenticated")
    try:
        context.verify(bytes.fromhex(x_signature))
    except (InvalidSignature, ValueError):
        raise HTTPException(status_code=401, detail="Invalid signature")
    return {"authenticated": True, "algorithm": context.algorithm.name}


client = TestClient(app)
valid = client.get(
    "/authenticated",
    headers={"X-Signature": signature(b"GET /authenticated")},
)
invalid = client.get(
    "/authenticated",
    headers={"X-Signature": signature(b"wrong route")},
)
print(valid.status_code, json.dumps(valid.json(), sort_keys=True))
print(invalid.status_code, json.dumps(invalid.json(), sort_keys=True))
