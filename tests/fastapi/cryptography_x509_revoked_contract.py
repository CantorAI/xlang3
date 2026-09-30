import json
from pathlib import Path

from cryptography import x509
from fastapi import FastAPI
from fastapi.testclient import TestClient


ROOT = Path(__file__).parent
app = FastAPI()


@app.get("/cryptography-x509-revoked")
def revoked_entries():
    crl = x509.load_pem_x509_crl((ROOT / "x509_crl_sample.pem").read_bytes())
    empty = x509.load_pem_x509_crl((ROOT / "x509_crl_empty_sample.pem").read_bytes())
    entry = crl[0]
    try:
        crl.get_revoked_certificate_by_serial_number("42")
    except TypeError as exc:
        invalid_serial_type = type(exc).__name__
    else:
        invalid_serial_type = "accepted"
    return {
        "entry_type": type(entry).__name__,
        "serial": entry.serial_number,
        "date": entry.revocation_date_utc.isoformat(),
        "extensions": len(entry.extensions),
        "lookup": crl.get_revoked_certificate_by_serial_number(42).serial_number,
        "missing": crl.get_revoked_certificate_by_serial_number(999999),
        "invalid_serial_type": invalid_serial_type,
        "iteration": [item.serial_number for item in crl],
        "empty_iteration": [item.serial_number for item in empty],
        "slice": [item.serial_number for item in crl[:]],
    }


response = TestClient(app).get("/cryptography-x509-revoked")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
