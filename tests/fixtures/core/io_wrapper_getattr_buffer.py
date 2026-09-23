import io
import os


class BufferProxy:
    def __init__(self, target):
        self.target = target

    def __getattr__(self, name):
        return getattr(self.target, name)


path = "xlang3_io_wrapper_getattr.tmp"
raw = io.FileIO(path, "w+")
wrapper = io.TextIOWrapper(
    BufferProxy(raw), encoding="utf-8", newline="", write_through=True
)
print(wrapper.fileno() == raw.fileno())
print(wrapper.write("caf\u00e9\n"))
wrapper.seek(0)
print(wrapper.read() == "caf\u00e9\n")
wrapper.close()
os.remove(path)
