import ipaddress
import json

from cryptography import x509
from cryptography.x509.oid import ExtendedKeyUsageOID
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/cryptography-x509-extension-der")
def extension_der():
    values = {
        "basic_ca": x509.BasicConstraints(ca=True, path_length=3),
        "basic_ee": x509.BasicConstraints(ca=False, path_length=None),
        "subject_key_identifier": x509.SubjectKeyIdentifier(bytes(range(20))),
        "key_usage": x509.KeyUsage(True, False, False, False, False,
                                    True, True, False, False),
        "authority_key_identifier": x509.AuthorityKeyIdentifier(
            bytes(range(20)), None, None
        ),
        "extended_key_usage": x509.ExtendedKeyUsage([
            ExtendedKeyUsageOID.CLIENT_AUTH,
            ExtendedKeyUsageOID.SERVER_AUTH,
            ExtendedKeyUsageOID.CODE_SIGNING,
        ]),
        "subject_alt_name": x509.SubjectAlternativeName([
            x509.DNSName("example.test"),
            x509.RFC822Name("a@example.test"),
            x509.IPAddress(ipaddress.ip_address("127.0.0.1")),
            x509.IPAddress(ipaddress.ip_network("10.0.0.0/8")),
            x509.IPAddress(ipaddress.ip_address("2001:db8::1")),
        ]),
    }
    return {name: value.public_bytes().hex() for name, value in values.items()}


response = TestClient(app).get("/cryptography-x509-extension-der")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
