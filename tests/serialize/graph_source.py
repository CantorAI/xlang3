import math
import os
import xlang_graph_native

OFFSET = 7

def factorial(n):
    return 1 if n < 2 else n * factorial(n - 1)

def even(n):
    return n == 0 or odd(n - 1)

def odd(n):
    return n != 0 and even(n - 1)

def make_counter(start):
    value = start
    def add(delta=1):
        nonlocal value
        value += delta
        return value
    def current():
        return value
    return add, current

def defaults(value=5, *, scale=3):
    return (value + OFFSET) * scale

class Base:
    def base_value(self):
        return 10

class Item(Base):
    label = 'item'
    def __init__(self, value=4):
        self.value = value
    def total(self, delta=0):
        return self.base_value() + self.value + delta
    @property
    def doubled(self):
        return self.value * 2
    @staticmethod
    def square(value):
        return value * value
    @classmethod
    def make(cls, value):
        return cls(value)

class Slotted:
    __slots__ = ('value',)
    def __init__(self, value):
        self.value = value

class Derived(Item):
    def total(self, delta=0):
        return super().total(delta) + 100

class Meta(type):
    def description(cls):
        return cls.__name__

class Custom(metaclass=Meta):
    pass

add, current = make_counter(20)
item = Item(8)
slots = Slotted(11)
cycle = []
cycle.append(cycle)
shared = {'value': [1, 2, 3]}
pair = (shared, shared)
blob = bytes(range(256)) * 4096
view = memoryview(blob)[1:257]
mutable_blob = bytearray(b'abc')
native = xlang_graph_native.Box()
native.set(42, shared)

def verify(payload):
    assert not os.path.exists(__file__)
    assert factorial(6) == 720
    assert even(12) and odd(13)
    assert defaults() == 36
    assert defaults(3, scale=2) == 20
    assert add(2) == 22
    assert current() == 22
    assert add() == 23
    assert current() == 23
    assert payload['item'] is item
    assert item.total(2) == 20
    assert item.doubled == 16
    assert Item.square(5) == 25
    assert Item.make(3).total() == 13
    assert Derived(3).total(1) == 114
    assert Custom.description() == 'Custom'
    assert isinstance(item, Base)
    assert slots.value == 11
    assert payload['bound'].__self__ is item
    assert payload['bound'](1) == 19
    assert pair[0] is pair[1]
    pair[0]['value'].append(4)
    assert pair[1]['value'][-1] == 4
    assert cycle[0] is cycle
    assert len(blob) == 1048576 and blob[65537] == 1
    assert view == blob[1:257]
    mutable_blob[0] = 90
    assert mutable_blob == bytearray(b'Zbc')
    assert math.sqrt(81) == 9
    assert native.number() == 42 and native.data() is shared
    return 'ok'

payload = {'verify': verify, 'item': item, 'class': Item, 'bound': item.total,
           'pair': pair, 'cycle': cycle, 'blob': blob, 'add': add, 'current': current,
           'native': native}
