import _pickle
import pickle
import io

p = _pickle.PickleBuffer(b"abc")
print(bytes(p.raw()))
p.release()
try:
    p.raw()
except Exception as exc:
    print(type(exc).__name__)

payload = {"items": [1, "two"], "flag": False}
print(pickle.loads(pickle.dumps(payload, protocol=5)) == payload)

buffers = []
stream = io.BytesIO()
native_pickler = _pickle.Pickler(stream, protocol=5, buffer_callback=buffers.append)
native_pickler.dump(_pickle.PickleBuffer(b"buffer"))
print(len(buffers), bytes(buffers[0].raw()))
stream.seek(0)
print(_pickle.Unpickler(stream, buffers=buffers).load().raw().tobytes())

class PersistentPickler(_pickle.Pickler):
    def persistent_id(self, value):
        if value == "marker":
            return "persistent-marker"
        return None

class PersistentUnpickler(_pickle.Unpickler):
    def persistent_load(self, value):
        return "loaded:" + value

stream = io.BytesIO()
PersistentPickler(stream, protocol=4).dump(["marker"])
stream.seek(0)
print(PersistentUnpickler(stream).load())
