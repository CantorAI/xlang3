import gc
import gzip
import io
import sys


class Stream(io.RawIOBase):
    pass


stream = Stream()
stream.close()
stream.close()
print("closed", stream.closed)

unraisable = []
original_hook = sys.unraisablehook
sys.unraisablehook = lambda event: unraisable.append(type(event.exc_value).__name__)
try:
    for _ in range(2):
        with gzip.GzipFile(fileobj=io.BytesIO(gzip.compress(b"abc"))) as reader:
            print(reader.read())
    gc.collect()
finally:
    sys.unraisablehook = original_hook
print("unraisable", unraisable)
