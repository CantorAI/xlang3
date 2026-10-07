import array
import sys


class Box:
    def __init__(self):
        self.items = [0, 0, 0]

    def __getitem__(self, index):
        return self.items[index]

    def __setitem__(self, index, value):
        self.items[index] = value
        return 12345  # Assignment must discard this without clobbering self.


def fill(box, count):
    for index in range(count):
        box[index % 3] = index
    return box


box = Box()
assert fill(box, 9) is box and box.items == [6, 7, 8]
# Special methods come from the type, even when an instance stores a callable.
box.__setitem__ = lambda index, value: (_ for _ in ()).throw(AssertionError())
box.__getitem__ = lambda index: -999
assert fill(box, 3) is box and box[2] == 2


def change_setter(box, count):
    original = Box.__setitem__
    try:
        for index in range(count):
            if index == 3:
                Box.__setitem__ = lambda self, key, value: self.items.__setitem__(key, value * 2)
            box[0] = index
        return box.items[0]
    finally:
        Box.__setitem__ = original


assert change_setter(box, 8) == 14
assert fill(box, 3) is box and box.items == [0, 1, 2]


class Child(Box):
    def __setitem__(self, key, value):
        super().__setitem__(key, value + 100)


child = Child()
assert fill(child, 3) is child and child.items == [100, 101, 102]


class NativeArray(array.array):
    __getitem__ = array.array.__getitem__
    __setitem__ = array.array.__setitem__


class ChangingIndex:
    def __index__(self):
        NativeArray.__getitem__ = lambda self, key: 900 + key
        return 1


values = NativeArray("i", [10, 20, 30])


def read_native(values, key):
    return values[key]


def write_native(values, key, value):
    values[key] = value
    return values


for _ in range(8):
    assert read_native(values, 1) == 20
assert read_native(values, ChangingIndex()) == 20
assert read_native(values, 1) == 901
NativeArray.__getitem__ = array.array.__getitem__
assert read_native(values, 1) == 20


class ChangingSetIndex:
    def __index__(self):
        NativeArray.__setitem__ = lambda self, key, value: array.array.__setitem__(self, key, value * 2)
        return 1


for _ in range(8):
    assert write_native(values, 1, 20) is values
assert write_native(values, ChangingSetIndex(), 70) is values
assert values[1] == 70
assert write_native(values, 1, 11) is values
assert values[1] == 22
NativeArray.__setitem__ = array.array.__setitem__
assert write_native(values, 1, 12) is values
assert values[1] == 12


class DescriptorBox:
    def __init__(self):
        self.items = [0]

    @property
    def __setitem__(self):
        return lambda key, value: self.items.__setitem__(key, value + 4)

    @property
    def __getitem__(self):
        return lambda key: self.items[key]


descriptor = DescriptorBox()
for _ in range(5):
    descriptor[0] = 10
assert descriptor[0] == 14


class GeneratorSetter:
    def __init__(self):
        self.called = False

    def __setitem__(self, key, value):
        self.called = True
        yield value


generator = GeneratorSetter()
generator[0] = 10
assert not generator.called


class Consumer:
    def __setitem__(self, key, value):
        assert value[0] == 97
        return None


def temporary_export():
    owner = bytearray(b"a")
    consumer = Consumer()
    consumer[0] = memoryview(owner)
    owner.extend(b"b")  # The consumed temporary must release before this line.
    return owner


assert temporary_export() == bytearray(b"ab")

events = []


def profile(frame, event, argument):
    if frame.f_code.co_name == "__setitem__" and event in ("call", "return"):
        events.append((event, argument if event == "return" else None))


box = Box()
sys.setprofile(profile)
try:
    fill(box, 5)
finally:
    sys.setprofile(None)
assert sum(event == "call" for event, _ in events) == 5
assert [argument for event, argument in events if event == "return"] == [12345] * 5
assert box.items == [3, 4, 2]


class BrokenSetter:
    def __setitem__(self, key, value):
        raise ValueError("setter body")


def invoke_broken(box):
    box[0] = 1


try:
    invoke_broken(BrokenSetter())
except ValueError as error:
    assert str(error) == "setter body"
    frames = []
    traceback = error.__traceback__
    while traceback is not None:
        frames.append(traceback.tb_frame.f_code.co_name)
        traceback = traceback.tb_next
    assert "invoke_broken" in frames and "__setitem__" in frames
else:
    raise AssertionError("setter exception was lost")

print("subscription dispatch semantics ok")
