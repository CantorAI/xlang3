import hashlib
import json
from pathlib import Path

from cryptography.hazmat.bindings._rust import openssl
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import dsa, utils
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/cryptography-dsa-parameters")
def cryptography_dsa_parameters():
    source = json.loads(Path(__file__).with_name("dsa-sample.json").read_text())
    p, q, g = (int(source[name], 16) for name in ("p", "q", "g"))
    numbers = openssl.dsa.DSAParameterNumbers(p, q, g)
    restored = numbers.parameters().parameter_numbers()
    generated = openssl.dsa.generate_parameters(1024).parameter_numbers()
    private = numbers.parameters().generate_private_key()
    public = private.public_key()
    private_numbers = private.private_numbers()
    public_numbers = public.public_numbers()
    rebuilt_private = dsa.DSAPrivateNumbers(
        private_numbers.x,
        dsa.DSAPublicNumbers(public_numbers.y, public_numbers.parameter_numbers),
    ).private_key()
    rebuilt_public = dsa.DSAPublicNumbers(
        public_numbers.y, public_numbers.parameter_numbers
    ).public_key()
    message = b"xlang3 dsa production route"
    signature = rebuilt_private.sign(message, hashes.SHA256())
    rebuilt_public.verify(signature, message, hashes.SHA256())
    invalid_signature = None
    try:
        rebuilt_public.verify(signature, message + b"!", hashes.SHA256())
    except InvalidSignature as exc:
        invalid_signature = type(exc).__name__
    digest = hashlib.sha256(message).digest()
    prehashed_signature = private.sign(digest, utils.Prehashed(hashes.SHA256()))
    public.verify(prehashed_signature, digest, utils.Prehashed(hashes.SHA256()))
    errors = []
    for values in ((q, p, g), (p, q, 1), (p, q, p), ("bad", q, g)):
        try:
            openssl.dsa.DSAParameterNumbers(*values).parameters()
        except Exception as exc:
            errors.append(f"{type(exc).__name__}: {exc}")
    return {
        "bits": [restored.p.bit_length(), restored.q.bit_length()],
        "generated_bits": [generated.p.bit_length(), generated.q.bit_length()],
        "roundtrip": (restored.p, restored.q, restored.g) == (p, q, g),
        "key_bits": [private.key_size, public.key_size],
        "number_roundtrip": (
            rebuilt_private.private_numbers().x == private_numbers.x
            and rebuilt_public.public_numbers().y == public_numbers.y
        ),
        "signature_valid": 40 <= len(signature) <= 50,
        "prehashed_valid": 40 <= len(prehashed_signature) <= 50,
        "invalid_signature": invalid_signature,
        "parameter_digest": hashlib.sha256(
            f"{restored.p:x}:{restored.q:x}:{restored.g:x}".encode()
        ).hexdigest(),
        "errors": errors,
    }


response = TestClient(app).get("/cryptography-dsa-parameters")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
