import io
import marshal

stream = io.BytesIO()
marshal.dump(0.0, stream, 1)
stream.seek(0)
print(marshal.load(stream) == 0.0)

stream = io.BytesIO()
marshal.dump({"version": 2}, stream, 2)
stream.seek(0)
print(marshal.load(stream))
