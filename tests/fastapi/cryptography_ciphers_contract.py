import json

from cryptography.exceptions import AlreadyFinalized, InvalidTag
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()
KEY = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
IV = bytes.fromhex("101112131415161718191a1b1c1d1e1f")
MESSAGE = bytes.fromhex("00112233445566778899aabbccddeeff")


@app.get("/cryptography-ciphers")
def cryptography_ciphers():
    result = {}
    for mode in (modes.CBC(IV), modes.CTR(IV), modes.ECB()):
        cipher = Cipher(algorithms.AES(KEY), mode)
        encryptor = cipher.encryptor()
        encrypted = encryptor.update(MESSAGE) + encryptor.finalize()
        decryptor = cipher.decryptor()
        result[mode.name.lower()] = {
            "encrypted": encrypted.hex(),
            "roundtrip": decryptor.update(encrypted) + decryptor.finalize() == MESSAGE,
        }
    gcm = Cipher(algorithms.AES(KEY), modes.GCM(IV[:12])).encryptor()
    gcm.authenticate_additional_data(b"associated")
    encrypted = gcm.update(MESSAGE) + gcm.finalize()
    decryptor = Cipher(algorithms.AES(KEY), modes.GCM(IV[:12], gcm.tag)).decryptor()
    decryptor.authenticate_additional_data(b"associated")
    result["gcm"] = {
        "encrypted": encrypted.hex(),
        "tag": gcm.tag.hex(),
        "roundtrip": decryptor.update(encrypted) + decryptor.finalize() == MESSAGE,
    }
    invalid = Cipher(algorithms.AES(KEY), modes.GCM(IV[:12], bytes(16))).decryptor()
    invalid.authenticate_additional_data(b"associated")
    invalid.update(encrypted)
    try:
        invalid.finalize()
    except InvalidTag as exc:
        result["invalid_tag"] = type(exc).__name__
    try:
        gcm.update(MESSAGE)
    except AlreadyFinalized as exc:
        result["finalized"] = type(exc).__name__
    counter = Cipher(algorithms.AES(KEY), modes.CTR(IV)).encryptor()
    first = counter.update(MESSAGE)
    counter.reset_nonce(IV)
    result["reset_nonce"] = counter.update(MESSAGE) == first
    return result


response = TestClient(app).get("/cryptography-ciphers")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
