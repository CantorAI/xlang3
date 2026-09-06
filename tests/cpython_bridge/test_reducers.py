import sys
import os
import subprocess
import array
import collections
import datetime
import decimal
import functools
import copyreg
import re
import threading
sys.path.insert(0, sys.argv[1])
import xlang3
import reducer_fixture as fixture


def roundtrip(value):
    return xlang3.loads(xlang3.dumps(value), trusted=True)


copyreg.pickle(fixture.Registered, fixture.reduce_registered)
values = [array.array("d", [1.25, -4.5, 100.0]),
          datetime.datetime(2026, 9, 5, 12, 34, tzinfo=datetime.timezone.utc),
          decimal.Decimal("1.234567890123456789"),
          collections.defaultdict(int, {"a": 3}),
          collections.OrderedDict([("b", 2), ("a", 1)]),
          functools.partial(pow, 2), re.compile(r"a+b", re.IGNORECASE)]
clones = roundtrip(values)
assert clones[0] == values[0] and clones[0].typecode == "d"
assert clones[1:5] == values[1:5]
assert clones[3]["missing"] == 0
assert list(clones[4]) == ["b", "a"]
assert clones[5](5) == 32
assert clones[6].fullmatch("AAAb")

queue = collections.deque(maxlen=10)
queue.append(queue)
queue.append(42)
clone = roundtrip([queue, queue])
assert clone[0] is clone[1] and clone[0][0] is clone[0]
assert clone[0].maxlen == 10 and clone[0][1] == 42

items = fixture.WithItems()
items.append(items)
clone = roundtrip(items)
assert type(clone) is fixture.WithItems and clone[0] is clone and clone.tag == 42

arguments = [1, {"binary": b"\x00\xff"}, []]
arguments[2].append(arguments)
record = fixture.Record(arguments)
clone, cloned_args = roundtrip([record, arguments])
assert clone.values is cloned_args and clone.restored
assert cloned_args[2][0] is cloned_args and cloned_args[1]["binary"] == b"\x00\xff"
assert roundtrip(fixture.SINGLETON) is fixture.SINGLETON
assert roundtrip(fixture.Registered(42)).value == 42
constructed = roundtrip(fixture.Constructed())
assert constructed.created == 42 and constructed.restored
slotted = roundtrip(fixture.Slotted())
assert slotted.value == 42 and slotted.self is slotted
assert roundtrip(fixture.Holder.SINGLETON) is fixture.Holder.SINGLETON
error = roundtrip(ValueError("example"))
assert type(error) is ValueError and error.args == ("example",)

large = array.array("I", range(262144))
clone = roundtrip([large, large])
assert clone[0] == large and clone[0] is clone[1]

for recipe in [(int,), (42, ()), (int, []), (int, (), None, []),
               (int, (), None, None, iter([("x",)])),
               (int, (), None, None, None, 42)]:
    try:
        xlang3.dumps(fixture.BadRecipe(recipe))
    except TypeError:
        pass
    else:
        raise AssertionError("invalid reducer accepted")
try:
    roundtrip(fixture.ConstructorCycle())
except (TypeError, ValueError):
    pass
else:
    raise AssertionError("cyclic native construction accepted")
assert roundtrip(fixture.BadRecipe((lambda: [42], ()))) == [42]


def local_graph():
    counter = {"calls": 0}

    def adjustment(value, *, offset=2):
        return value + offset

    class Local:
        def __init__(self, value):
            counter["calls"] += 1
            self.value = adjustment(value)

        def answer(self):
            return self.value

        def __reduce_ex__(self, protocol):
            return rebuild, (self.value - 2,), {"self": self}

        def __setstate__(self, state):
            assert state["self"] is self
            self.restored = True

    def rebuild(value=40):
        return Local(value)

    instance = Local(40)
    return instance, Local, rebuild, counter


local = local_graph()
restored_local = roundtrip(local)
instance, cls, rebuild, counter = restored_local
assert type(instance) is cls and instance.answer() == 42 and instance.restored
assert counter["calls"] == 2
assert rebuild().answer() == 42 and counter["calls"] == 3
assert local[3]["calls"] == 1

def class_constructor():
    class Local:
        def __init__(self, value=40):
            self.value = value + 2
    return fixture.BadRecipe((Local, (40,)))

assert roundtrip(class_constructor()).value == 42
stateful_arg = fixture.Constructed()
assert roundtrip(fixture.BadRecipe((lambda obj: obj.created + obj.restored,
                                   (stateful_arg,)))) == 43
local_partial = functools.partial(lambda value, *, offset=2: value + offset, 40)
assert roundtrip(local_partial)() == 42

def callable_dependencies():
    class Builder:
        def __init__(self):
            self.base = 40
        def __call__(self, offset):
            return self.base + offset
        def build(self, offset):
            return self.base + offset
    builder = Builder()
    return (fixture.BadRecipe((builder, (2,))),
            fixture.BadRecipe((builder.build, (2,))))

assert roundtrip(callable_dependencies()) == (42, 42)

def recursive_constructor():
    def factorial(n):
        return 1 if n == 0 else n * factorial(n - 1)
    return fixture.BadRecipe((lambda: factorial(5), ()))

assert roundtrip(recursive_constructor()) == 120

def descriptor_constructor():
    calls = {"names": 0}
    class Descriptor:
        def __set_name__(self, owner, name):
            calls["names"] += 1
            self.name = name
        def __get__(self, instance, owner):
            return 2
    class Base:
        def answer(self):
            return 40
    class Local(Base):
        __slots__ = ("value",)
        offset = Descriptor()
        def __init__(self):
            self.value = super().answer() + self.offset
    return fixture.BadRecipe((Local, ())), calls

descriptor_value, descriptor_calls = roundtrip(descriptor_constructor())
assert descriptor_value.value == 42 and descriptor_calls["names"] == 2

def raise_from_constructor():
    raise ValueError("constructor failure")

try:
    roundtrip(fixture.BadRecipe((raise_from_constructor, ())))
except ValueError as error:
    assert str(error) == "constructor failure"
else:
    raise AssertionError("constructor exception was swallowed")
assert roundtrip(fixture.BadRecipe((lambda: 42, ()))) == 42
try:
    xlang3.dumps(threading.Lock())
except TypeError:
    pass
else:
    raise AssertionError("lock silently serialized")

env = os.environ.copy()
env["PYTHONPATH"] = os.pathsep.join([sys.argv[1], os.path.dirname(__file__)])
code = """import sys, xlang3, reducer_fixture
x = xlang3.loads(sys.stdin.buffer.read(), trusted=True)
assert x[0].typecode == 'd' and x[0][0] == 1.25
assert x[5](5) == 32 and x[6].fullmatch('AAAb')
assert x[7][0] is x[7] and x[7].tag == 42
print('fresh-process native reducers PASS')
"""
result = subprocess.run([sys.executable, "-S", "-c", code], input=xlang3.dumps(values + [items]),
                        capture_output=True, env=env, timeout=20)
assert result.returncode == 0, result.stderr.decode()
print(result.stdout.decode().strip())
local_code = """import sys, xlang3
obj, cls, rebuild, counter = xlang3.loads(sys.stdin.buffer.read(), trusted=True)
assert type(obj) is cls and obj.answer() == 42 and obj.restored
assert counter['calls'] == 2
assert rebuild().answer() == 42 and counter['calls'] == 3
print('fresh-process local reducer dependencies PASS')
"""
result = subprocess.run([sys.executable, "-S", "-c", local_code], input=xlang3.dumps(local),
                        capture_output=True, env=env, timeout=20)
assert result.returncode == 0, result.stderr.decode()
print(result.stdout.decode().strip())
native = xlang3.importModule("cpython", fromPath=sys.argv[2])
hosted = native.loads(native.dumps(values + [items]), True)
assert hosted[0] == values[0] and hosted[5](5) == 32
assert hosted[7][0] is hosted[7] and hosted[7].tag == 42
hosted_local = native.loads(native.dumps(local), True)
assert type(hosted_local[0]) is hosted_local[1] and hosted_local[0].answer() == 42
assert hosted_local[3]["calls"] == 2
print("Native reducers: arrays, datetime, decimal, mappings, partial, regex, cycles, state setters PASS")
