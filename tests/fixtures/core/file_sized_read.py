import tempfile
from pathlib import Path


with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "data.bin"
    data = b"<file content>" * 1000
    path.write_bytes(data)
    with open(path, "rb") as stream:
        first = stream.read(65536)
        print(len(first), first == data, stream.tell(), stream.read(1))
    with open(path, "rb", buffering=0) as stream:
        first = stream.read(65536)
        print(len(first), first == data)
