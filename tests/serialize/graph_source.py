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

def live_default(*, scale=1):
    return scale

def shared_default(*, scale=0):
    return scale

keyword_defaults = live_default.__kwdefaults__
shared_default.__kwdefaults__ = keyword_defaults
keyword_defaults['scale'] = 11

class KeywordDefaults(dict):
    def __getitem__(self, key):
        raise AssertionError('default binding used __getitem__')

def subclass_default(*, scale=0):
    return scale

subclass_defaults = KeywordDefaults(scale=17)
subclass_default.__kwdefaults__ = subclass_defaults

def swapped(value=5):
    return value + OFFSET

def replacement_body(value=100):
    return value * OFFSET

replacement_code = replacement_body.__code__.replace(co_filename='restored-code.py')
swapped.__code__ = replacement_code

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
    assert swapped() == 35
    assert swapped.__code__ is payload['replacement_code']
    assert swapped.__code__.co_filename == 'restored-code.py'
    assert factorial(6) == 720
    assert even(12) and odd(13)
    assert defaults() == 36
    assert defaults(3, scale=2) == 20
    assert live_default.__kwdefaults__ is shared_default.__kwdefaults__
    assert live_default.__kwdefaults__ is payload['keyword_defaults']
    assert live_default() == shared_default() == 11
    keyword_defaults['scale'] = 33
    assert live_default() == shared_default() == 33
    assert subclass_default.__kwdefaults__ is payload['subclass_defaults']
    assert subclass_default() == 17
    subclass_defaults['scale'] = 19
    assert subclass_default() == 19
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
           'native': native, 'keyword_defaults': keyword_defaults,
           'subclass_defaults': subclass_defaults, 'replacement_code': replacement_code}
