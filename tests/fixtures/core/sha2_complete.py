import _sha2


expected = {
    "sha224": "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7",
    "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
    "sha384": "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7",
    "sha512": "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
}

for name, digest in expected.items():
    constructor = getattr(_sha2, name)
    value = constructor(b"abc")
    print(name, value.hexdigest() == digest, len(value.digest()), value.digest_size, value.block_size)
    copied = constructor(b"a")
    copied.update(bytearray(b"b"))
    clone = copied.copy()
    clone.update(memoryview(b"c"))
    print(clone.hexdigest() == digest, copied.hexdigest() != digest)
