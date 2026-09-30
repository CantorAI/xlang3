import hashlib
import json
from pathlib import Path

from cryptography.hazmat.bindings._rust import x509 as native_x509
from cryptography.hazmat.primitives import _serialization, hashes
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/cryptography-x509-reader")
def cryptography_x509_reader():
    pem = Path(__file__).with_name("x509-sample.pem").read_bytes()
    certificate = native_x509.load_pem_x509_certificate(pem)
    der = certificate.public_bytes(_serialization.Encoding.DER)
    loaded_der = native_x509.load_der_x509_certificate(der)
    chain = native_x509.load_pem_x509_certificates(pem + pem)
    return {
        "serial": certificate.serial_number,
        "der_serial": loaded_der.serial_number,
        "der_size": len(der),
        "fingerprint": certificate.fingerprint(hashes.SHA256()).hex(),
        "digest_matches": hashlib.sha256(der).digest()
        == certificate.fingerprint(hashes.SHA256()),
        "pem_roundtrip": certificate.public_bytes(_serialization.Encoding.PEM)
        == pem,
        "chain_count": len(chain),
    }


response = TestClient(app).get("/cryptography-x509-reader")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
