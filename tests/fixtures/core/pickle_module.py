import _pickle
import pickle

p = _pickle.PickleBuffer(b"abc")
print(bytes(p.raw()))
p.release()
try:
    p.raw()
except Exception as exc:
    print(type(exc).__name__)

payload = {"items": [1, "two"], "flag": False}
print(pickle.loads(pickle.dumps(payload, protocol=5)) == payload)
