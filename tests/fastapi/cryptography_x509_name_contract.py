import json

from cryptography import x509
from cryptography.x509.name import _ASN1Type
from cryptography.x509.oid import NameOID
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/cryptography-x509-name")
def encode_names():
    attr = x509.NameAttribute
    simple = x509.Name([
        attr(NameOID.COUNTRY_NAME, "US"),
        attr(NameOID.ORGANIZATION_NAME, "CantorAI"),
        attr(NameOID.COMMON_NAME, "example.test"),
    ])
    grouped = x509.Name([
        x509.RelativeDistinguishedName([
            attr(NameOID.ORGANIZATION_NAME, "Cafe\u00e9"),
            attr(NameOID.ORGANIZATIONAL_UNIT_NAME, "R&D"),
        ]),
        x509.RelativeDistinguishedName([
            attr(NameOID.COMMON_NAME, "example"),
        ]),
    ])
    explicit = x509.Name([
        attr(NameOID.COMMON_NAME, "name", _type=_ASN1Type.BMPString),
        attr(NameOID.EMAIL_ADDRESS, "test@example.com"),
    ])
    return {
        "simple_der": simple.public_bytes().hex(),
        "grouped_der": grouped.public_bytes().hex(),
        "explicit_der": explicit.public_bytes().hex(),
        "empty_der": x509.Name([]).public_bytes().hex(),
    }


response = TestClient(app).get("/cryptography-x509-name")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
