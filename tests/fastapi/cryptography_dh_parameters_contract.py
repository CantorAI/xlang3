import hashlib
import json
from pathlib import Path

from cryptography.hazmat.bindings._rust import openssl
from cryptography.hazmat.primitives import _serialization
from cryptography.hazmat.primitives.asymmetric import dh
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/cryptography-dh-parameters")
def cryptography_dh_parameters():
    pem = Path(__file__).with_name("dh-sample.pem").read_bytes()
    parameters = openssl.dh.from_pem_parameters(pem)
    numbers = parameters.parameter_numbers()
    der = parameters.parameter_bytes(
        _serialization.Encoding.DER,
        _serialization.ParameterFormat.PKCS3,
    )
    restored = openssl.dh.DHParameterNumbers(
        numbers.p, numbers.g, numbers.q
    ).parameters()
    alice = parameters.generate_private_key()
    bob = parameters.generate_private_key()
    alice_public = alice.public_key()
    bob_public = bob.public_key()
    alice_secret = alice.exchange(bob_public)
    bob_secret = bob.exchange(alice_public)
    private_numbers = alice.private_numbers()
    public_numbers = alice_public.public_numbers()
    restored_private = dh.DHPrivateNumbers(
        private_numbers.x,
        dh.DHPublicNumbers(public_numbers.y, public_numbers.parameter_numbers),
    ).private_key()
    restored_public = dh.DHPublicNumbers(
        public_numbers.y, public_numbers.parameter_numbers
    ).public_key()
    invalid = []
    for p, g in ((23, 5), (2**511 + 1, 1), (2**512 + 1, 2)):
        try:
            openssl.dh.DHParameterNumbers(p, g).parameters()
        except ValueError as exc:
            invalid.append(str(exc))
    invalid_key_numbers = []
    for make in (
        lambda: dh.DHPublicNumbers("4", numbers),
        lambda: dh.DHPrivateNumbers("4", public_numbers),
        lambda: dh.DHPublicNumbers(4, None),
    ):
        try:
            make()
        except TypeError as exc:
            invalid_key_numbers.append(str(exc))
    return {
        "bits": numbers.p.bit_length(),
        "generator": numbers.g,
        "subgroup_order": numbers.q,
        "der_sha256": hashlib.sha256(der).hexdigest(),
        "pem_roundtrip": parameters.parameter_bytes(
            _serialization.Encoding.PEM,
            _serialization.ParameterFormat.PKCS3,
        ) == pem,
        "restored": restored.parameter_numbers().p == numbers.p,
        "exchange": alice_secret == bob_secret,
        "secret_bytes": len(alice_secret),
        "private_bits": alice.key_size,
        "public_bits": bob_public.key_size,
        "key_parameters": (
            alice.parameters().parameter_numbers().p == numbers.p
            and bob_public.parameters().parameter_numbers().g == numbers.g
        ),
        "number_roundtrip": (
            restored_private.exchange(bob_public) == alice_secret
            and bob.exchange(restored_public) == bob_secret
        ),
        "number_types": [
            type(private_numbers).__name__,
            type(public_numbers).__name__,
        ],
        "invalid": invalid,
        "invalid_key_numbers": invalid_key_numbers,
    }


response = TestClient(app).get("/cryptography-dh-parameters")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
