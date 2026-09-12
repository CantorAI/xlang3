import _pickle
import pickle
import io
import gc
import weakref

for invalid_buffer in ("text",):
    try:
        _pickle.PickleBuffer(invalid_buffer)
    except Exception as exc:
        print(type(exc).__name__)

released_view = memoryview(b"released")
released_view.release()
try:
    _pickle.PickleBuffer(released_view)
except Exception as exc:
    print(type(exc).__name__)

p = _pickle.PickleBuffer(b"abc")
print(bytes(p.raw()))
p.release()
try:
    p.raw()
except Exception as exc:
    print(type(exc).__name__)
try:
    memoryview(p)
except Exception as exc:
    print(type(exc).__name__)

class BufferOwner(bytes):
    pass

owner = BufferOwner(b"cycle")
cycle_buffer = _pickle.PickleBuffer(owner)
owner.cycle = cycle_buffer
cycle_ref = weakref.ref(cycle_buffer)
del owner, cycle_buffer
gc.collect()
print(cycle_ref() is None)

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
print(memo_pickler.fast)
memo_pickler.fast = 1
print(memo_pickler.fast)
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
print(second is first, third is first, len(memo_unpickler.memo) > 0)

cycle = []
cycle.append(cycle)
stream = io.BytesIO()
_pickle.Pickler(stream, protocol=4).dump(cycle)
stream.seek(0)
loaded_cycle = _pickle.Unpickler(stream).load()
print(loaded_cycle is loaded_cycle[0])

default_pickler = _pickle.Pickler(io.BytesIO())
print(default_pickler.persistent_id("value") is None)
try:
    _pickle.Unpickler(io.BytesIO()).persistent_load("id")
except _pickle.UnpicklingError:
    print("persistent load unsupported")
print(_pickle.Unpickler(io.BytesIO()).find_class("builtins", "list") is list)


memo_unpickler = _pickle.Unpickler(io.BytesIO())
for invalid_memo in (object(), {-1: None}):
    try:
        memo_unpickler.memo = invalid_memo
    except Exception as exc:
        print(type(exc).__name__)
memo_unpickler.memo = {1: None}
print(memo_unpickler.memo[1] is None)
