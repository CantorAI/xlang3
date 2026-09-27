import json
import hashlib

from cryptography.hazmat.bindings._rust import openssl
from cryptography.hazmat.primitives.serialization import (
    Encoding, NoEncryption, PrivateFormat, PublicFormat,
)
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/cryptography-x25519")
def cryptography_x25519():
    alice = openssl.x25519.from_private_bytes(bytes(range(32)))
    bob = openssl.x25519.from_private_bytes(bytes(range(32, 64)))
    alice_public = alice.public_key()
    bob_public = bob.public_key()
    generated = openssl.x25519.generate_key()
    private_der = alice.private_bytes(
        Encoding.DER, PrivateFormat.PKCS8, NoEncryption()
    )
    public_der = alice_public.public_bytes(
        Encoding.DER, PublicFormat.SubjectPublicKeyInfo
    )
    bad_lengths = []
    for data in (b"", b"\x00" * 31, b"\x00" * 33):
        for load in (openssl.x25519.from_private_bytes,
                     openssl.x25519.from_public_bytes):
            try:
                load(data)
            except ValueError as exc:
                bad_lengths.append((len(data), type(exc).__name__))
    try:
        alice.exchange(openssl.x25519.from_public_bytes(bytes(32)))
        low_order = "accepted"
    except ValueError as exc:
        low_order = str(exc)
    return {
        "alice_public": alice_public.public_bytes_raw().hex(),
        "bob_public": bob_public.public_bytes_raw().hex(),
        "shared": alice.exchange(bob_public).hex(),
        "symmetric": alice.exchange(bob_public) == bob.exchange(alice_public),
        "generated": len(generated.private_bytes_raw()) == 32 and
                     len(generated.public_key().public_bytes_raw()) == 32 and
                     generated.exchange(alice_public) == alice.exchange(generated.public_key()),
        "private_raw": alice.private_bytes_raw().hex(),
        "private_der_sha256": hashlib.sha256(private_der).hexdigest(),
        "public_der_sha256": hashlib.sha256(public_der).hexdigest(),
        "copy": [alice.__copy__() == alice,
                 alice_public.__copy__() == alice_public],
        "bad_lengths": bad_lengths,
        "low_order": low_order,
    }


response = TestClient(app).get("/cryptography-x25519")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
