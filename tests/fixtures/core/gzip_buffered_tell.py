import gzip
import io


compressed = gzip.compress(b"abcd")
stream = gzip.GzipFile(fileobj=io.BytesIO(compressed), mode="rb")
print(stream.tell(), stream.read(2), stream.tell())
print(stream.seek(0), stream.read(1), stream.tell())
