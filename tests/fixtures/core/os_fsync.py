import os
import tempfile


with tempfile.TemporaryFile() as file:
    file.write(b"payload")
    file.flush()
    os.fsync(file.fileno())
    file.seek(0)
    print(file.read())
    closed_fd = file.fileno()

try:
    os.fsync(closed_fd)
except OSError as exc:
    print(type(exc).__name__, exc.errno)
