"""Separate GzipFile decompression from its inherited iteration protocol."""

import gzip
import io

payload = b"first\nsecond\n"
stream = gzip.GzipFile(fileobj=io.BytesIO(gzip.compress(payload)), mode="rb")
print("decompression works:", stream.read() == payload)
stream.seek(0)
try:
    print("line iteration works:", list(stream) == [b"first\n", b"second\n"])
except TypeError as error:
    print("line iteration works:", False)
    print("iteration failure:", str(error))
stream.close()
