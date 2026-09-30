import datetime
import json
from pathlib import Path

from cryptography.hazmat.bindings._rust import x509
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()
ROOT = Path(__file__).parent


@app.get("/cryptography-x509-client-policy")
def cryptography_x509_client_policy():
    ca = x509.load_pem_x509_certificate((ROOT / "x509_verify_root.pem").read_bytes())
    server_leaf = x509.load_pem_x509_certificate((ROOT / "x509_verify_leaf.pem").read_bytes())
    client_no_san = x509.load_pem_x509_certificate(
        (ROOT / "x509_verify_client_no_san.pem").read_bytes()
    )
    at = datetime.datetime(2025, 7, 1, tzinfo=datetime.timezone.utc)
    verifier = x509.PolicyBuilder().store(x509.Store([ca])).time(at).build_client_verifier()
    try:
        verifier.verify(server_leaf, [])
        rejection = "accepted"
    except x509.VerificationError as exc:
        rejection = type(exc).__name__
    try:
        verifier.verify(client_no_san, [])
        no_san_rejection = "accepted"
    except x509.VerificationError as exc:
        no_san_rejection = type(exc).__name__
    return {
        "client_type": type(verifier).__name__,
        "subject": verifier.policy.subject,
        "eku": verifier.policy.extended_key_usage.dotted_string,
        "store_type": type(verifier.store).__name__,
        "server_leaf": rejection,
        "client_without_san": no_san_rejection,
    }


response = TestClient(app).get("/cryptography-x509-client-policy")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
