import json

from cryptography.hazmat.primitives import _serialization
from cryptography.hazmat.primitives.serialization import load_pem_private_key
from cryptography.hazmat.primitives.asymmetric import ec
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/cryptography-ec")
def cryptography_ec():
    curve = ec.SECP256R1()
    key = ec.derive_private_key(7, curve)
    public_numbers = key.public_key().public_numbers()
    private_numbers = key.private_numbers()
    restored = private_numbers.private_key()
    point = key.public_key().public_bytes(
        _serialization.Encoding.X962,
        _serialization.PublicFormat.CompressedPoint,
    )
    decoded = ec.EllipticCurvePublicKey.from_encoded_point(curve, point)
    private_der = key.private_bytes(
        _serialization.Encoding.DER,
        _serialization.PrivateFormat.PKCS8,
        _serialization.NoEncryption(),
    )
    return {
        "curve": curve.name,
        "key_size": key.key_size,
        "private_value": private_numbers.private_value,
        "x": hex(public_numbers.x),
        "y": hex(public_numbers.y),
        "restored": restored.public_key().public_numbers().x == public_numbers.x,
        "point": point.hex(),
        "decoded": decoded.public_numbers().x == public_numbers.x,
        "private_der_size": len(private_der),
    }


response = TestClient(app).get("/cryptography-ec")
print(response.status_code, json.dumps(response.json(), sort_keys=True))


@app.get("/cryptography-ec-encrypted")
def cryptography_ec_encrypted():
    key = ec.derive_private_key(7, ec.SECP256R1())
    encoded = key.private_bytes(
        _serialization.Encoding.PEM,
        _serialization.PrivateFormat.TraditionalOpenSSL,
        _serialization.BestAvailableEncryption(b"password"),
    )
    restored = load_pem_private_key(encoded, b"password")
    return {
        "curve": restored.curve.name,
        "round_trip": (
            restored.private_numbers().private_value
            == key.private_numbers().private_value
        ),
        "encrypted_pem": b"ENCRYPTED" in encoded,
    }


response = TestClient(app).get("/cryptography-ec-encrypted")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
