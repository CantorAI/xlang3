# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import hashlib
import hmac

vectors = {
    "md5": "900150983cd24fb0d6963f7d28e17f72",
    "sha1": "a9993e364706816aba3e25717850c26c9cd0d89d",
    "sha224": "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7",
    "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
    "sha384": "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7",
    "sha512": "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
    "sha3_256": "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532",
}
for algorithm, expected in vectors.items():
    value = getattr(hashlib, algorithm)(b"abc")
    assert value.hexdigest() == expected
    copied = value.copy()
    copied.update(b"d")
    assert copied.hexdigest() != value.hexdigest()
print("standard hash vectors ok")

assert hashlib.shake_128(b"abc").hexdigest(16) == "5881092dd818bf5cf8a3ddb793fbcba7"
assert hmac.new(b"key", b"The quick brown fox jumps over the lazy dog", "sha256").hexdigest() == "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8"
assert hmac.digest(b"key", b"message", "sha256") == hmac.new(b"key", b"message", "sha256").digest()
assert hmac.compare_digest(b"abc", b"abc") and not hmac.compare_digest(b"abc", b"abd")
assert hashlib.pbkdf2_hmac("sha256", b"password", b"salt", 1).hex() == "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"
assert hashlib.scrypt(b"password", salt=b"NaCl", n=1024, r=8, p=16, dklen=64).hex() == "fdbabe1c9d3472007856e7190d01e9fe7c6ad7cbc8237830e77376634b3731622eaf30d92e22a3886ff109279d9830dac727afb94a83ee6d8360cbdfa2cc0640"
print("hmac and kdf vectors ok")

assert hashlib.blake2b(b"").hexdigest() == "786a02f742015903c6c6fd852552d272912f4740e15847618a86e217f71f5419d25e1031afee585313896444934eb04b903a685b1448b755d56f701afe9be2ce"
assert hashlib.blake2s(b"abc").hexdigest() == "508c5e8c327c14e2e1a72ba34eeb452f37458b209ed63a294d999b4c86675982"
assert hashlib.blake2b(b"foo", digest_size=16, key=b"bar", salt=b"baz", person=b"bing", fanout=2, depth=3, leaf_size=4, node_offset=5, node_depth=6, inner_size=7, last_node=True).hexdigest() == "920568b0c5873b2f0ab67bedb6cf1b2b"
assert hashlib.blake2s(b"foo", digest_size=16, key=b"bar", salt=b"baz", person=b"bing", fanout=2, depth=3, leaf_size=4, node_offset=5, node_depth=6, inner_size=7, last_node=True).hexdigest() == "bf2a8f7fe3c555012a6f8046e646bc75"
keyed = hashlib.blake2b(key=b"secret")
keyed.update(b"first")
copied = keyed.copy()
keyed.update(b"a")
copied.update(b"b")
assert keyed.hexdigest() != copied.hexdigest()
print("blake2 vectors and parameters ok")
