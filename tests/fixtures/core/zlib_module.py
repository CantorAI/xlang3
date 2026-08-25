import zlib

payload = b"abc abc abc abc"
compressed = zlib.compress(payload)
print(zlib.decompress(compressed) == payload)
print(len(compressed) < len(payload) + 12)
print(zlib.crc32(payload) == zlib.crc32(payload, 0))
print(zlib.adler32(payload) == zlib.adler32(payload, 1))
print(zlib.ZLIB_VERSION, zlib.DEFLATED, zlib.MAX_WBITS)

c = zlib.compressobj()
streamed = c.compress(b"abc ") + c.compress(b"abc ") + c.flush()
d = zlib.decompressobj()
roundtrip = d.decompress(streamed[:4]) + d.decompress(streamed[4:]) + d.flush()
print(roundtrip == b"abc abc ")
print(d.eof, d.unused_data == b"", d.unconsumed_tail == b"")

trailing = zlib.compress(b"xyz") + b"tail"
d2 = zlib.decompressobj()
print(d2.decompress(trailing) == b"xyz")
print(d2.eof, d2.unused_data == b"tail")
