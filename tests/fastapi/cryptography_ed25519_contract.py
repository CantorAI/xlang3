import copy
import hashlib
import json

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.bindings._rust import openssl
from cryptography.hazmat.primitives import _serialization
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()
SEED = bytes.fromhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")


@app.get("/cryptography-ed25519")
def cryptography_ed25519():
    private = openssl.ed25519.from_private_bytes(SEED)
    public = private.public_key()
    signature = private.sign(b"")
    public.verify(signature, b"")
    invalid = None
    try:
        public.verify(signature, b"changed")
    except InvalidSignature as exc:
        invalid = type(exc).__name__
    public_pem = public.public_bytes(
        _serialization.Encoding.PEM,
        _serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    private_der = private.private_bytes(
        _serialization.Encoding.DER,
        _serialization.PrivateFormat.PKCS8,
        _serialization.NoEncryption(),
    )
    encrypted = private.private_bytes(
        _serialization.Encoding.PEM,
        _serialization.PrivateFormat.PKCS8,
        _serialization.BestAvailableEncryption(b"route-secret"),
    )
    generated = openssl.ed25519.generate_key()
    generated_signature = generated.sign(b"generated key")
    generated.public_key().verify(generated_signature, b"generated key")
    return {
        "public_hex": public.public_bytes_raw().hex(),
        "signature_sha256": hashlib.sha256(signature).hexdigest(),
        "raw_private": private.private_bytes_raw() == SEED,
        "copy_private": copy.copy(private).public_key().public_bytes_raw() == public.public_bytes_raw(),
        "copy_public": copy.copy(public).public_bytes_raw() == public.public_bytes_raw(),
        "public_pem_sha256": hashlib.sha256(public_pem).hexdigest(),
        "private_der_sha256": hashlib.sha256(private_der).hexdigest(),
        "encrypted_pem": encrypted.startswith(b"-----BEGIN ENCRYPTED PRIVATE KEY-----"),
        "invalid_signature": invalid,
        "generated_key": len(generated.private_bytes_raw()) == 32,
    }


response = TestClient(app).get("/cryptography-ed25519")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
