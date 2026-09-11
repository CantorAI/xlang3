import zlib

payload = b"abc abc abc abc"
compressed = zlib.compress(payload)
print(zlib.decompress(compressed) == payload)
print(len(compressed) < len(payload) + 12)
print(zlib.crc32(payload) == zlib.crc32(payload, 0))
print(zlib.adler32(payload) == zlib.adler32(payload, 1))
print(zlib.ZLIB_VERSION, zlib.DEFLATED, zlib.MAX_WBITS)
print(zlib.ZLIB_RUNTIME_VERSION == zlib.ZLIB_VERSION)

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
d2.decompress(b"more")
print(d2.unused_data == b"tailmore", d2.decompress(b"") == b"")

c3 = zlib.compressobj()
head = c3.compress(b"head")
c4 = c3.copy()
left = zlib.decompress(head + c3.compress(b"-left") + c3.flush())
right = zlib.decompress(head + c4.compress(b"-right") + c4.flush())
print(left, right)
import copy
c5 = zlib.compressobj()
c6 = copy.deepcopy(c5)
print(zlib.decompress(c5.compress(b"a") + c5.flush()), zlib.decompress(c6.compress(b"b") + c6.flush()))

encoded = zlib.compress(b"copy-me")
d3 = zlib.decompressobj()
first = d3.decompress(encoded[:5])
d4 = d3.copy()
print(first + d3.decompress(encoded[5:]), first + d4.decompress(encoded[5:]))

dictionary = b"dictionary payload"
with_dict = zlib.compressobj(zdict=dictionary)
dict_encoded = with_dict.compress(dictionary) + with_dict.flush()
print(zlib.decompressobj(zdict=dictionary).decompress(dict_encoded) == dictionary)
try:
    zlib.decompressobj().decompress(dict_encoded)
except Exception as exc:
    print(type(exc).__name__)
