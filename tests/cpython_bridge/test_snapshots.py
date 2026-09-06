import sys
import subprocess
import os
import struct
import types
import gc
import weakref
sys.path.insert(0, sys.argv[1])
import xlang3

class NoCloudpickle:
    def find_spec(self, fullname, path=None, target=None):
        if fullname == "cloudpickle" or fullname.startswith("cloudpickle."):
            raise AssertionError("snapshot attempted to import cloudpickle")

sys.meta_path.insert(0, NoCloudpickle())

def roundtrip(value):
    return xlang3.loads(xlang3.dumps(value), trusted=True)

def factory(offset):
    class Local:
        def __init__(self, value):
            self.value = value
        def answer(self):
            return self.value + offset
    return Local

obj = factory(5)(37)
root = {"object": obj, "function": lambda value: value + 2, "binary": b"a\x00b" * 65536}
root["self"] = root
root["alias"] = obj
blob = xlang3.dumps(root)
try:
    xlang3.loads(blob)
except ValueError:
    pass
else:
    raise AssertionError("untrusted loading was accepted")
restored = xlang3.loads(blob, trusted=True)
assert restored["self"] is restored and restored["alias"] is restored["object"]
assert restored["object"].answer() == 42
assert restored["function"](40) == 42
assert restored["binary"] == root["binary"]
environment = os.environ.copy()
environment["PYTHONPATH"] = sys.argv[1]
code = "import sys,xlang3; x=xlang3.loads(sys.stdin.buffer.read(),trusted=True); assert x['self'] is x; assert x['object'].answer()==42; assert x['function'](40)==42; print('fresh-process snapshot PASS')"
result = subprocess.run([sys.executable, "-S", "-c", code], input=blob, capture_output=True, env=environment, timeout=20)
assert result.returncode == 0, result.stderr.decode()
print(result.stdout.decode().strip())

for value in [None, False, True, Ellipsis, NotImplemented, 0, -1, 2**4096, -(2**4096),
              1.25, complex(2.5, -7.25), "a\x00b\ud800", {1, 2}, frozenset({1, 2})]:
    assert roundtrip(value) == value
for size in [0, 1, 257, 65536, 1048576, 4194304]:
    data = bytearray(b"\x00\xff" * (size // 2) + b"x" * (size % 2))
    copy = roundtrip([data, data, bytes(data)])
    assert copy[0] == data and copy[0] is copy[1] and type(copy[0]) is bytearray
    assert type(copy[2]) is bytes and copy[2] == data

items = []
cycle = (items,)
items.append(cycle)
clone = roundtrip(cycle)
assert clone[0][0] is clone

def recursive_pair():
    def even(n):
        return n == 0 or odd(n - 1)
    def odd(n):
        return n != 0 and even(n - 1)
    return even, odd

even, odd = roundtrip(recursive_pair())
assert even(20) and odd(21) and not even(21)

def shared_cells():
    count = 0
    def advance():
        nonlocal count
        count += 1
        return count
    def current():
        return count
    return advance, current

advance, current = roundtrip(shared_cells())
assert advance.__closure__[0] is current.__closure__[0]
assert advance() == current() == 1

def empty_cell():
    missing = 1
    def read():
        return missing
    del missing
    return read

empty = roundtrip(empty_cell())
try:
    empty()
except NameError:
    pass
else:
    raise AssertionError("empty cell was filled")

GLOBAL_COUNT = 0
def global_increment():
    global GLOBAL_COUNT
    GLOBAL_COUNT += 1
    return GLOBAL_COUNT
def global_current():
    return GLOBAL_COUNT

increment, current = roundtrip((global_increment, global_current))
assert increment.__globals__ is current.__globals__
assert increment() == current() == 1 and GLOBAL_COUNT == 0

def function_state(a: int = 40, *, b: int = 2) -> int:
    return a + b
function_state.self = function_state
function_state.__defaults__ = (function_state,)
function_clone = roundtrip(function_state)
assert function_clone.self is function_clone
assert function_clone.__defaults__[0] is function_clone
assert function_clone(40) == 42
assert function_clone.__annotations__ == function_state.__annotations__

def class_factory():
    class Base:
        def answer(self):
            return 40
    class Derived(Base):
        __slots__ = ("value",)
        def __init__(self):
            self.value = 2
        def answer(self):
            return super().answer() + self.value
        @staticmethod
        def twice(value):
            return value * 2
        @classmethod
        def create(cls):
            return cls()
        @property
        def result(self):
            return self.answer()
        @result.setter
        def result(self, value):
            self.value = value - 40
    return Derived

cls = class_factory()
instance = cls()
class_clone, instance_clone, bound = roundtrip((cls, instance, instance.answer))
assert type(instance_clone) is class_clone
assert bound.__self__ is instance_clone and bound() == 42
assert class_clone.twice(21) == 42 and class_clone.create().result == 42
instance_clone.result = 45
assert instance_clone.result == 45

def state_factory():
    class WithState:
        def __getstate__(self):
            return {"number": self.number, "self": self}
        def __setstate__(self, state):
            assert state["self"] is self
            self.number = state["number"]
        def __hash__(self):
            return self.number
    return WithState

stateful = state_factory()()
stateful.number = 42
state_clone = roundtrip({stateful: stateful})
key = next(iter(state_clone))
assert key.number == 42 and state_clone[key] is key
assert roundtrip(len) is len and roundtrip(sys) is sys

# Dropping the restored root must release self-referential classes/functions too.
references = []
for _ in range(20):
    restored_class = roundtrip(class_factory())
    references.append(weakref.ref(restored_class))
    del restored_class
gc.collect()
assert all(ref() is None for ref in references)

deep = []
cursor = deep
for _ in range(10000):
    child = []
    cursor.append(child)
    cursor = child
cursor = roundtrip(deep)
for _ in range(10000):
    cursor = cursor[0]
assert cursor == []

# Validate record framing before performing imports or running restore hooks.
def envelope(payload):
    return blob[:16] + struct.pack("<Q", len(payload)) + payload

for payload in [struct.pack("<III", 1, 0, 0),
                struct.pack("<III", 1, 1000001, 0),
                struct.pack("<III", 1, 1, 0) + struct.pack("<BQI", 255, 0, 0),
                struct.pack("<III", 1, 1, 0) + struct.pack("<BQII", 11, 0, 1, 2),
                struct.pack("<III", 1, 1, 0) + struct.pack("<BQII", 12, 0, 1, 0),
                struct.pack("<III", 1, 1, 0) + struct.pack("<BQI", 11, 0, 8000001)]:
    try:
        xlang3.loads(envelope(payload), trusted=True)
    except ValueError:
        pass
    else:
        raise AssertionError("malformed graph accepted")

custom_builtins_function = types.FunctionType(
    (lambda: len(())).__code__, {"__builtins__": {"len": lambda x: 42}})
assert roundtrip(custom_builtins_function)() == 42
frozen = roundtrip(frozenset([stateful]))
assert next(iter(frozen)).number == 42
sequence = [1, 2, 3]
iterator = iter(sequence)
assert next(iterator) == 1
sequence, iterator = roundtrip((sequence, iterator))
sequence[1] = 42
assert list(iterator) == [42, 3]
for unsupported in [(x for x in range(2)), memoryview(b"abc")]:
    try:
        xlang3.dumps(unsupported)
    except TypeError:
        pass
    else:
        raise AssertionError("unsupported runtime object silently serialized")
gc.collect()
for invalid in [b"", blob[:12], blob[:-1], blob + b"x", blob[:12] + b"\x00" * 4 + blob[16:],
                blob[:8] + struct.pack("<I", 1) + blob[12:]]:
    try:
        xlang3.loads(invalid, trusted=True)
    except ValueError:
        pass
    else:
        raise AssertionError("invalid snapshot accepted")
module = xlang3.importModule("bridge_fixture", fromPath=sys.argv[2])
clone = xlang3.loads(xlang3.dumps(module.add), trusted=True)
assert clone(39, right=3) == 42
native = xlang3.importModule("cpython", fromPath=sys.argv[3])
assert native.loads(native.dumps(root), True)["object"].answer() == 42
print("Snapshots: cycles, binary, bigint, recursive functions, shared cells, class descriptors/state, malformed input PASS")
