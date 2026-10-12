"""Run with xlang3; direct native binding until other AEAD classes are implemented."""
import json
from pathlib import Path
from cryptography.exceptions import InvalidTag
from cryptography.hazmat.bindings._rust import openssl

AESGCM = openssl.aead.AESGCM


def expect(exception, action):
    try:
        action()
    except exception:
        return
    raise AssertionError("Expected " + exception.__name__)


zero = AESGCM(bytes(16))
nonce = bytes(12)
assert zero.encrypt(nonce, b"", None).hex() == "58e2fccefa7e3061367f1d57a4e7455a"
assert zero.encrypt(nonce, bytes(16), None).hex() == "0388dace60b6a392f328c2b971b2fe78ab6e47d42cec13bdf53a67b21257bddf"
vectors = json.loads(Path(__file__).with_name("aesgcm_vectors.json").read_text(encoding="utf-8"))
for vector in vectors:
    key, nonce, data, aad, ciphertext = [bytes.fromhex(vector[name]) for name in ("key", "nonce", "data", "aad", "ciphertext")]
    cipher = AESGCM(key=key)
    assert cipher.encrypt(nonce=nonce, data=data, associated_data=aad) == ciphertext
    assert cipher.decrypt(nonce, ciphertext, aad) == data
    corrupted = ciphertext[:-1] + bytes([ciphertext[-1] ^ 1])
    expect(InvalidTag, lambda: cipher.decrypt(nonce, corrupted, aad))
    expect(InvalidTag, lambda: cipher.decrypt(nonce, ciphertext, aad + b"wrong"))
    expect(InvalidTag, lambda: AESGCM(bytes([key[0] ^ 1]) + key[1:]).decrypt(nonce, ciphertext, aad))
    expect(InvalidTag, lambda: cipher.decrypt(bytes([nonce[0] ^ 1]) + nonce[1:], ciphertext, aad))

for size in [0, 15, 17, 31, 33]:
    expect(ValueError, lambda: AESGCM(bytes(size)))
expect(TypeError, lambda: AESGCM("text"))
for size in [0, 7, 129]:
    expect(ValueError, lambda: zero.encrypt(bytes(size), b"", None))
expect(InvalidTag, lambda: zero.decrypt(bytes(12), b"short", None))
expect(TypeError, lambda: zero.encrypt(bytes(12), "text", None))
expect(TypeError, lambda: zero.encrypt(bytes(12), b"data", "text"))
expect(TypeError, lambda: zero.encrypt(bytes(12), b"data", None, unknown=True))
for bits in [128, 192, 256]:
    key = AESGCM.generate_key(bit_length=bits)
    assert isinstance(key, bytes) and len(key) == bits // 8
expect(ValueError, lambda: AESGCM.generate_key(127))
expect(TypeError, lambda: AESGCM.generate_key("128"))
expect(TypeError, lambda: AESGCM.generate_key(128, bit_length=128))
mutable_key = bytearray(range(16))
cipher = AESGCM(mutable_key)
encrypted = cipher.encrypt(bytes(12), memoryview(b"binary\0data"), bytearray(b"aad"))
mutable_key[0] ^= 1
assert cipher.decrypt(bytes(12), encrypted, b"aad") == b"binary\0data"
print("AESGCM vectors, authentication rejection, keys, buffers and API errors passed")
