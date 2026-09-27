import json

from cryptography.hazmat.primitives import _serialization, hashes
from cryptography.hazmat.bindings._rust import openssl
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/rsa-operations")
def rsa_operations() -> dict:
    private = rsa.generate_private_key(65537, 2048)
    public = private.public_key()
    data = b"FastAPI on XLang3"
    signature = private.sign(data, padding.PKCS1v15(), hashes.SHA256())
    public.verify(signature, data, padding.PKCS1v15(), hashes.SHA256())
    oaep = padding.OAEP(
        mgf=padding.MGF1(hashes.SHA256()),
        algorithm=hashes.SHA256(),
        label=b"fastapi-test",
    )
    encrypted = public.encrypt(data, oaep)
    public_pem = public.public_bytes(
        _serialization.Encoding.PEM,
        _serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    private_pem = private.private_bytes(
        _serialization.Encoding.PEM,
        _serialization.PrivateFormat.TraditionalOpenSSL,
        _serialization.NoEncryption(),
    )
    numbers = private.private_numbers()
    loaded_private = openssl.keys.load_pem_private_key(
        private_pem, password=None
    )
    loaded_public = openssl.keys.load_pem_public_key(public_pem)
    encrypted_pem = private.private_bytes(
        _serialization.Encoding.PEM,
        _serialization.PrivateFormat.PKCS8,
        _serialization.BestAvailableEncryption(b"password"),
    )
    loaded_encrypted = openssl.keys.load_pem_private_key(
        encrypted_pem, password=b"password"
    )
    return {
        "key_bits": private.key_size,
        "public_exponent": numbers.public_numbers.e,
        "reconstructed_key": numbers.private_key().key_size,
        "signature_bytes": len(signature),
        "decrypted": private.decrypt(encrypted, oaep).decode(),
        "public_pem": public_pem.splitlines()[0].decode(),
        "private_pem": private_pem.splitlines()[0].decode(),
        "encrypted_pem": encrypted_pem.splitlines()[0].decode(),
        "loaded_private": loaded_private.public_key().public_numbers().n == numbers.public_numbers.n,
        "loaded_public": loaded_public.public_numbers().n == numbers.public_numbers.n,
        "loaded_encrypted": loaded_encrypted.public_key().public_numbers().n == numbers.public_numbers.n,
    }


response = TestClient(app).get("/rsa-operations")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
