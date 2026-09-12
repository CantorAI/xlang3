import zlib

payload = b"abc abc abc abc"
compressed = zlib.compress(payload)
print(zlib.decompress(compressed) == payload)
print(len(compressed) < len(payload) + 12)
print(zlib.crc32(payload) == zlib.crc32(payload, 0))
print(zlib.adler32(payload) == zlib.adler32(payload, 1))
print(zlib.ZLIB_VERSION, zlib.DEFLATED, zlib.MAX_WBITS)
print(zlib.ZLIB_RUNTIME_VERSION == zlib.ZLIB_VERSION)

rle = zlib.compressobj(strategy=zlib.Z_RLE)
rle_data = rle.compress(b"aaaaabbbbb") + rle.flush()
print(zlib.decompress(rle_data) == b"aaaaabbbbb",
      all(isinstance(getattr(zlib, name), int) for name in (
          "Z_PARTIAL_FLUSH", "Z_BLOCK", "Z_TREES", "Z_FILTERED",
          "Z_HUFFMAN_ONLY", "Z_RLE", "Z_FIXED")))

c = zlib.compressobj()
streamed = c.compress(b"abc ") + c.compress(b"abc ") + c.flush()
d = zlib.decompressobj()
roundtrip = d.decompress(streamed[:4]) + d.decompress(streamed[4:]) + d.flush()
print(roundtrip == b"abc abc ")
print(d.eof, d.unused_data == b"", d.unconsumed_tail == b"")

bounded = zlib.decompressobj()
bounded_first = bounded.decompress(zlib.compress(b"flush-me" * 100), 1)
bounded_copy = bounded.copy()
bounded_tail = (b"flush-me" * 100)[1:]
print(bounded_first, bounded.flush() == bounded_tail, bounded_copy.flush() == bounded_tail,
      bounded.eof, bounded.unconsumed_tail == b"")

trailing = zlib.compress(b"xyz") + b"tail"
d2 = zlib.decompressobj()
print(d2.decompress(trailing) == b"xyz")
print(d2.eof, d2.unused_data == b"tail")
d2.decompress(b"more")
print(d2.unused_data == b"tailmore", d2.decompress(b"") == b"")
completed_copy = d2.copy()
print(completed_copy.eof, completed_copy.unused_data == b"tailmore")

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

dictionary_copy = zlib.decompressobj(zdict=dictionary)
dictionary_copy_clone = dictionary_copy.copy()
print(dictionary_copy.decompress(dict_encoded) == dictionary,
      dictionary_copy_clone.decompress(dict_encoded) == dictionary)

gzip_stream = zlib.compressobj(wbits=16 + zlib.MAX_WBITS)
gzip_data = gzip_stream.compress(b"gzip") + gzip_stream.flush()
print(zlib.decompress(gzip_data, 32 + zlib.MAX_WBITS))

finished = zlib.compressobj()
finished.flush()
for operation in (lambda: finished.flush(), lambda: finished.compress(b"x"), lambda: zlib.compressobj().flush(999)):
    try:
        operation()
    except Exception as exc:
        print(type(exc).__name__)

for operation in (
    lambda: zlib.decompress(compressed, "invalid"),
    lambda: zlib.decompress(compressed, zlib.MAX_WBITS, "invalid"),
    lambda: zlib.decompress(compressed, zlib.MAX_WBITS, -1),
):
    try:
        operation()
    except Exception as exc:
        print(type(exc).__name__)

try:
    zlib.decompress(zlib.compress(b"window"), 14)
except zlib.error as exc:
    print("invalid window size" in str(exc))
keyword_compressed = zlib.compress(b"keyword", level=1)
print(zlib.decompress(keyword_compressed, wbits=zlib.MAX_WBITS, bufsize=zlib.DEF_BUF_SIZE))
keyword_stream = zlib.compressobj()
keyword_data = keyword_stream.compress(b"stream") + keyword_stream.flush()
print(zlib.decompressobj().decompress(keyword_data, max_length=6))
raw_compressed = zlib.compress(b"raw", wbits=-zlib.MAX_WBITS)
print(zlib.decompress(raw_compressed, wbits=-zlib.MAX_WBITS))
gzip_compressed = zlib.compress(b"gzip-keyword", wbits=16 + zlib.MAX_WBITS)
print(zlib.decompress(gzip_compressed, wbits=32 + zlib.MAX_WBITS))
try:
    zlib.decompressobj().decompress(b"", -1)
except ValueError:
    print("negative max length")

terminal_decompressor = zlib.decompressobj()
terminal_decompressor.decompress(zlib.compress(b"terminal-copy"))
terminal_decompressor.flush()
try:
    terminal_decompressor.copy()
except ValueError:
    print("terminal decompressor copy")

class _ZlibIndex:
    def __index__(self):
        return 64

print(zlib.decompress(zlib.compress(b"index-buffer"), bufsize=_ZlibIndex()))
try:
    zlib.decompress(zlib.compress(b"truncated")[:-1])
except zlib.error as exc:
    print("incomplete or truncated stream" in str(exc))

raw_dictionary = b"raw-dictionary"
raw_compressor = zlib.compressobj(wbits=-zlib.MAX_WBITS, zdict=raw_dictionary)
raw_data = raw_compressor.compress(raw_dictionary) + raw_compressor.flush()
print(zlib.decompressobj(wbits=-zlib.MAX_WBITS, zdict=raw_dictionary).decompress(raw_data) == raw_dictionary)

class _ZlibLength:
    def __index__(self):
        return 1

length_stream = zlib.decompressobj()
length_stream.decompress(zlib.compress(b"length"), _ZlibLength())
print(length_stream.flush(_ZlibLength()) == b"ength")

strict_stream = zlib._ZlibDecompressor()
strict_data = zlib.compress(b"strict-stream")
strict_head = strict_stream.decompress(strict_data, max_length=3)
print(strict_head, strict_stream.needs_input)
strict_tail = strict_stream.decompress(b"")
print(strict_head + strict_tail, strict_stream.eof, strict_stream.unused_data)
try:
    strict_stream.decompress(b"")
except EOFError:
    print("strict eof")
