import hashlib
import json
from pathlib import Path

from cryptography.hazmat.bindings._rust import x509
from cryptography.hazmat.primitives import _serialization
from cryptography.hazmat.primitives import hashes
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()
ROOT = Path(__file__).parent


@app.get("/cryptography-x509-documents")
def cryptography_x509_documents():
    csr_pem = (ROOT / "x509_csr_sample.pem").read_bytes()
    crl_pem = (ROOT / "x509_crl_sample.pem").read_bytes()
    empty_crl_pem = (ROOT / "x509_crl_empty_sample.pem").read_bytes()
    csr = x509.load_pem_x509_csr(csr_pem)
    crl = x509.load_pem_x509_crl(crl_pem)
    csr_der = csr.public_bytes(_serialization.Encoding.DER)
    crl_der = crl.public_bytes(_serialization.Encoding.DER)
    tampered_der = csr_der[:-1] + bytes([csr_der[-1] ^ 1])
    malformed = []
    for loader, data in (
        (x509.load_der_x509_csr, b"\x30\x00"),
        (x509.load_der_x509_crl, b"\x30\x00"),
        (x509.load_der_x509_csr, csr_der + b"extra"),
        (x509.load_der_x509_crl, crl_der + b"extra"),
    ):
        try:
            loader(data)
        except ValueError as exc:
            malformed.append(type(exc).__name__)
    return {
        "csr_type": type(csr).__name__,
        "csr_valid": csr.is_signature_valid,
        "csr_tampered_valid": x509.load_der_x509_csr(tampered_der).is_signature_valid,
        "csr_pem_roundtrip": csr.public_bytes(_serialization.Encoding.PEM) == csr_pem,
        "csr_der_sha256": hashlib.sha256(csr_der).hexdigest(),
        "csr_signature_sha256": hashlib.sha256(csr.signature).hexdigest(),
        "csr_tbs_sha256": hashlib.sha256(csr.tbs_certrequest_bytes).hexdigest(),
        "crl_type": type(crl).__name__,
        "crl_revoked_count": len(crl),
        "empty_crl_revoked_count": len(x509.load_pem_x509_crl(empty_crl_pem)),
        "malformed": malformed,
        "crl_der_revoked_count": len(x509.load_der_x509_crl(crl_der)),
        "crl_pem_roundtrip": crl.public_bytes(_serialization.Encoding.PEM) == crl_pem,
        "crl_der_sha256": hashlib.sha256(crl_der).hexdigest(),
        "crl_signature_sha256": hashlib.sha256(crl.signature).hexdigest(),
        "crl_tbs_sha256": hashlib.sha256(crl.tbs_certlist_bytes).hexdigest(),
        "crl_fingerprint_sha256": crl.fingerprint(hashes.SHA256()).hex(),
    }


response = TestClient(app).get("/cryptography-x509-documents")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
