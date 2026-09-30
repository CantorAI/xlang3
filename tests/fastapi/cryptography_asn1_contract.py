import json

from cryptography.hazmat.bindings._rust import asn1
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/cryptography-asn1")
def cryptography_asn1() -> dict:
    r, s = 2**130, 2**129
    encoded = asn1.encode_dss_signature(r, s)
    decoded = asn1.decode_dss_signature(encoded)
    spki = bytes.fromhex("302a300506032b6570032100") + bytes(range(32))
    try:
        asn1.encode_dss_signature(-1, 2)
    except ValueError as exc:
        negative_error = str(exc)
    return {
        "der": encoded.hex(),
        "r": str(decoded[0]),
        "s": str(decoded[1]),
        "subject_public_key": asn1.parse_spki_for_data(spki).hex(),
        "negative_error": negative_error,
    }


response = TestClient(app).get("/cryptography-asn1")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
