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

stream = io.BytesIO()
memo_pickler = _pickle.Pickler(stream, protocol=4)
shared = ["memo"]
memo_pickler.dump(shared)
memo_pickler.dump(shared)
print(len(memo_pickler.memo) > 0, memo_pickler.clear_memo() is None, len(memo_pickler.memo) == 0)
memo_pickler.dump(shared)
stream.seek(0)
memo_unpickler = _pickle.Unpickler(stream)
first = memo_unpickler.load()
second = memo_unpickler.load()
third = memo_unpickler.load()
print(second is first, third is first)

cycle = []
cycle.append(cycle)
stream = io.BytesIO()
_pickle.Pickler(stream, protocol=4).dump(cycle)
stream.seek(0)
loaded_cycle = _pickle.Unpickler(stream).load()
print(loaded_cycle is loaded_cycle[0])
