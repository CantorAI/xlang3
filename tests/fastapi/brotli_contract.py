import brotli
from fastapi import FastAPI
from fastapi.responses import Response
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/brotli")
def compressed_response():
    return Response(
        content=brotli.compress(b"production payload"),
        media_type="text/plain",
        headers={"content-encoding": "br"},
    )


response = TestClient(app).get("/brotli")
print(response.status_code, response.text, response.headers["content-encoding"])

compressed = brotli.compress(b"abc" * 100)
decoder = brotli.Decompressor()
part1 = decoder.process(compressed[:3])
part2 = decoder.process(compressed[3:])
restored = part1 + part2
print(compressed.hex(), len(restored), restored[:9], decoder.is_finished())

try:
    brotli.decompress(b"not brotli")
except brotli.error as exc:
    print(type(exc).__name__, str(exc))
