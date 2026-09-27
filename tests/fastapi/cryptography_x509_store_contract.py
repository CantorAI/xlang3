import hashlib
import json
from pathlib import Path

from cryptography.hazmat.bindings._rust import x509
from cryptography.hazmat.primitives import _serialization
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()
ROOT = Path(__file__).parent


@app.get("/cryptography-x509-store")
def cryptography_x509_store():
    certificate = x509.load_pem_x509_certificate(
        (ROOT / "x509_sct_sample.pem").read_bytes()
    )
    store = x509.Store([certificate])
    duplicate_store = x509.Store([certificate, certificate])
    failures = []
    for anchors in ([], [1]):
        try:
            x509.Store(anchors)
        except (TypeError, ValueError) as exc:
            failures.append(type(exc).__name__)
    return {
        "store_type": type(store).__name__,
        "duplicate_store_type": type(duplicate_store).__name__,
        "certificate_serial": certificate.serial_number,
        "certificate_der_sha256": hashlib.sha256(
            certificate.public_bytes(_serialization.Encoding.DER)
        ).hexdigest(),
        "invalid_anchor_errors": failures,
    }


response = TestClient(app).get("/cryptography-x509-store")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
