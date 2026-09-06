import sys
sys.path.insert(0, sys.argv[1])
import cpython

py = cpython.importModule("sys")
assert py.implementation.name == "cpython"
assert py.version_info[0] == 3
assert py.version_info[1] == 14
fixture = cpython.importModule("hosted_fixture", sys.argv[2])
counter = fixture.Counter(4)
assert counter.add(3) == 7
counter.value = 20
assert counter.value == 20
data = fixture.container()
assert data["binary"] == b"a\x00b"
assert len(data["values"]) == 3
total = 0
for item in data["values"]:
    total += item
assert total == 6

def callback(value, offset=0):
    return value + offset

assert fixture.invoke(callback, 39) == 42
assert fixture.threaded(callback, 39) == 42
fixture.retain(callback)
assert fixture.call_saved() == 42
try:
    fixture.fail()
except ValueError as error:
    assert "hosted CPython error" in str(error)
else:
    raise AssertionError("CPython exception lost")
try:
    cpython.importModule("__xlang3_missing_cpython_module__")
except ImportError:
    pass
else:
    raise AssertionError("missing module accepted")
assert fixture.invoke(callback, 39) == 42
# A compiled CPython extension, not an XLang3 builtin implementation.
sha = cpython.importModule("_sha2")
assert sha.sha256(b"abc").hexdigest() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
builtins = cpython.importModule("builtins")
python_bytes = builtins.bytearray(b"abc")
shared = cpython.buffer(python_bytes)
shared[0] = 90
assert python_bytes[0] == 90
encoded = cpython.dumps(fixture.container())
decoded = cpython.loads(encoded, True)
assert decoded["binary"] == b"a\x00b"
print("XLang3-hosted CPython: imports, extension, live objects, callbacks, threads PASS")
