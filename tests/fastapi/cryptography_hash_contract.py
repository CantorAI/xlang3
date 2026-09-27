import json

from cryptography.hazmat.bindings._rust import openssl
from cryptography.exceptions import _Reasons
from cryptography.hazmat.primitives import hashes
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/cryptography-hash")
def cryptography_hash() -> dict:
    sha = hashes.Hash(hashes.SHA256())
    sha.update(b"abc")
    copy = sha.copy()
    copy.update(b"def")
    digest = sha.finalize().hex()
    copied_digest = copy.finalize().hex()
    try:
        sha.update(b"again")
    except Exception as exc:
        finalized_error = [type(exc).__name__, str(exc)]

    shake = hashes.Hash(hashes.SHAKE128(16))
    shake.update(b"abc")
    shake_digest = shake.finalize().hex()

    xof = hashes.XOFHash(hashes.SHAKE128(16))
    xof.update(b"abc")
    xof_copy = xof.copy()
    xof_chunks = [xof.squeeze(5).hex(), xof.squeeze(7).hex()]
    return {
        "sha256": digest,
        "sha256_copy": copied_digest,
        "shake128": shake_digest,
        "xof_chunks": xof_chunks,
        "xof_copy": xof_copy.squeeze(12).hex(),
        "finalized_error": finalized_error,
        "sha256_supported": openssl.hashes.hash_supported(hashes.SHA256()),
        "unsupported_hash_reason": repr(_Reasons.UNSUPPORTED_HASH),
    }


response = TestClient(app).get("/cryptography-hash")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
