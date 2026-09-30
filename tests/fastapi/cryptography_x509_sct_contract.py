import json
from pathlib import Path

from cryptography.hazmat.bindings._rust import x509
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()
ROOT = Path(__file__).parent


@app.get("/cryptography-x509-sct")
def cryptography_x509_sct():
    certificate = x509.load_pem_x509_certificate(
        (ROOT / "x509_sct_sample.pem").read_bytes()
    )
    timestamps = certificate._signed_certificate_timestamps()
    return {
        "count": len(timestamps),
        "items": [
            {
                "type": type(item).__name__,
                "log_id": item.log_id.hex(),
                "timestamp": item.timestamp.isoformat(),
                "signature": item.signature.hex(),
                "signature_hash_algorithm": item.signature_hash_algorithm.name,
                "extension_bytes": item.extension_bytes.hex(),
            }
            for item in timestamps
        ],
    }


response = TestClient(app).get("/cryptography-x509-sct")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
