import zlib

payload = b"abc abc abc abc"
compressed = zlib.compress(payload)
print(zlib.decompress(compressed) == payload)
print(len(compressed) < len(payload) + 12)
print(zlib.crc32(payload) == zlib.crc32(payload, 0))
print(zlib.adler32(payload) == zlib.adler32(payload, 1))
print(zlib.ZLIB_VERSION, zlib.DEFLATED, zlib.MAX_WBITS)
