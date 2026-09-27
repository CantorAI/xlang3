import datetime
import json
from pathlib import Path

from cryptography.hazmat.bindings._rust import x509
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()
ROOT = Path(__file__).parent


@app.get("/cryptography-x509-verification")
def cryptography_x509_verification():
    ca = x509.load_pem_x509_certificate((ROOT / "x509_verify_root.pem").read_bytes())
    leaf = x509.load_pem_x509_certificate((ROOT / "x509_verify_leaf.pem").read_bytes())
    cn_only = x509.load_pem_x509_certificate((ROOT / "x509_verify_cn_only.pem").read_bytes())
    store = x509.Store([ca])
    at = int(datetime.datetime(2025, 7, 1, tzinfo=datetime.timezone.utc).timestamp())
    chain = store._verify_server_dns(leaf, [], "service.example", at)
    errors = []
    for candidate, hostname, when in (
        (leaf, "other.example", at),
        (leaf, "service.example", int(datetime.datetime(2028, 1, 1, tzinfo=datetime.timezone.utc).timestamp())),
        (cn_only, "service.example", at),
    ):
        try:
            store._verify_server_dns(candidate, [], hostname, when)
        except x509.VerificationError as exc:
            errors.append(type(exc).__name__)
    return {"chain_serials": [cert.serial_number for cert in chain], "errors": errors}


response = TestClient(app).get("/cryptography-x509-verification")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
