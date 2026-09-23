import gzip
import zlib


data = b"abc" * 10
print(zlib._ZlibDecompressor(wbits=15).decompress(zlib.compress(data)) == data)
print(gzip.decompress(gzip.compress(data)) == data)
