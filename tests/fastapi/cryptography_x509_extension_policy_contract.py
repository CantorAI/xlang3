import datetime
import json
from pathlib import Path

from cryptography.hazmat.bindings._rust import x509
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()
ROOT = Path(__file__).parent


@app.get("/cryptography-x509-extension-policy")
def cryptography_x509_extension_policy():
    ca = x509.load_pem_x509_certificate((ROOT / "x509_verify_root.pem").read_bytes())
    client = x509.load_pem_x509_certificate(
        (ROOT / "x509_verify_client_no_san.pem").read_bytes()
    )
    base = x509.PolicyBuilder().store(x509.Store([ca])).time(
        datetime.datetime(2025, 7, 1, tzinfo=datetime.timezone.utc)
    )
    with_policy = base.extension_policies(
        ca_policy=x509.ExtensionPolicy.webpki_defaults_ca(),
        ee_policy=x509.ExtensionPolicy.permit_all(),
    )
    verified = with_policy.build_client_verifier().verify(client, [])
    failures = []
    for operation in (
        lambda: base.extension_policies(
            ca_policy=x509.ExtensionPolicy.permit_all(),
            ee_policy=x509.ExtensionPolicy.permit_all(),
        ).build_client_verifier(),
        lambda: with_policy.extension_policies(
            ca_policy=x509.ExtensionPolicy.webpki_defaults_ca(),
            ee_policy=x509.ExtensionPolicy.webpki_defaults_ee(),
        ),
    ):
        try:
            operation()
        except ValueError as exc:
            failures.append(str(exc))
    return {
        "chain": [cert.serial_number for cert in verified.chain],
        "subjects": verified.subjects,
        "criticality": repr(x509.Criticality.CRITICAL),
        "failures": failures,
    }


response = TestClient(app).get("/cryptography-x509-extension-policy")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
