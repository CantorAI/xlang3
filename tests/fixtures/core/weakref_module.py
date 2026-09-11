import weakref
import _weakref
import operator


class Box:
    pass


b = Box()
b.name = "box"

r = weakref.ref(b)
print(r().name)
print(weakref.ref(b) is r)
print(type(r).__name__, type(r).__module__, weakref.ReferenceType is type(r))
print(weakref.proxy(b).name)
proxy = weakref.proxy(b)
proxy.extra = 7
del proxy.extra
print(b.extra if hasattr(b, "extra") else "removed")
print(weakref.getweakrefcount(b))
print(sorted(type(item).__name__ for item in weakref.getweakrefs(b)))

r2 = _weakref.ref(b)
print(r2().name)
print(weakref.ReferenceType(b)().name)
print(weakref.ref(b) == weakref.ref(b), weakref.ref(b) != weakref.ref(Box()))

callback_probe = Box()
def callback_property(reference):
    return reference
no_callback = weakref.ref(callback_probe)
with_callback = weakref.ref(callback_probe, callback_property)
print(no_callback.__callback__ is None, with_callback.__callback__ is callback_property)

class CallableBox:
    def __call__(self, value):
        return value + 1

callable_box = CallableBox()
callable_proxy = weakref.proxy(callable_box)
print(callable_proxy(4), type(callable_proxy).__name__)

class KeywordCallableBox:
    def __call__(self, *, value):
        return value + 2

keyword_callable_box = KeywordCallableBox()
print(weakref.proxy(keyword_callable_box)(value=5))

class SequenceBox:
    def __init__(self):
        self.values = [3, 4]
    def __len__(self):
        return len(self.values)
    def __iter__(self):
        return iter(self.values)
    def __getitem__(self, index):
        return self.values[index]
    def __contains__(self, value):
        return value in self.values

sequence_proxy = weakref.proxy(SequenceBox())
print(len(sequence_proxy), list(sequence_proxy), sequence_proxy[1], 3 in sequence_proxy)

class IterOnlyBox:
    def __iter__(self):
        return iter([8, 9])

print(8 in weakref.proxy(IterOnlyBox()), 4 in weakref.proxy(IterOnlyBox()))

class FalseBox:
    def __bool__(self):
        return False

false_box = FalseBox()
print(bool(weakref.proxy(false_box)))

class LenOnlyBox:
    def __len__(self):
        return 0

class PlainTruthBox:
    pass

print(bool(weakref.proxy(LenOnlyBox())), bool(weakref.proxy(PlainTruthBox())))

class ComparableBox:
    def __eq__(self, value):
        return value == "match"

comparable_box = ComparableBox()
comparable_proxy = weakref.proxy(comparable_box)
print(comparable_proxy == "match", comparable_proxy != "miss")
try:
    hash(comparable_proxy)
except Exception as exc:
    print(type(exc).__name__)

class StringBox:
    def __str__(self):
        return "referent-string"

string_box = StringBox()
print(str(weakref.proxy(string_box)))

class MutableBox:
    def __init__(self):
        self.values = [1, 2, 3]
    def __setitem__(self, index, value):
        self.values[index] = value
    def __delitem__(self, index):
        del self.values[index]

mutable_values = MutableBox()
mutable_proxy = weakref.proxy(mutable_values)
mutable_proxy[1] = 9
del mutable_proxy[0]
print(mutable_values.values)

expired = weakref.ref(Box())
import gc
gc.collect()
print(expired() is None)
hashed = Box()
hashed_ref = weakref.ref(hashed)
live_hash = hash(hashed_ref)
del hashed
gc.collect()
print(hash(hashed_ref) == live_hash)
unhashed_ref = weakref.ref(Box())
gc.collect()
try:
    hash(unhashed_ref)
except Exception as exc:
    print(type(exc).__name__)
callback_order = []
callback_target = Box()
first_callback = weakref.ref(callback_target, lambda ref: callback_order.append("first"))
second_callback = weakref.ref(callback_target, lambda ref: callback_order.append("second"))
del callback_target
gc.collect()
print(callback_order)
events = []
callback_ref = weakref.ref(Box(), lambda ref: events.append(ref() is None))
gc.collect()
print(events)
proxy_events = []
callback_proxy = weakref.proxy(Box(), lambda ref: proxy_events.append(type(ref).__name__))
gc.collect()
print(proxy_events)
class NumericBox:
    def __repr__(self):
        return "numeric-box"
    def __add__(self, value):
        return value + 4
    def __sub__(self, value):
        return value - 1
    def __mul__(self, value):
        return value * 2
    def __neg__(self):
        return -4
    def __invert__(self):
        return 8
    def __int__(self):
        return 4
    def __bytes__(self):
        return b"numeric"

numeric_box = NumericBox()
numeric_proxy = weakref.proxy(numeric_box)
print(repr(numeric_proxy), numeric_proxy + 3, int(numeric_proxy), bytes(numeric_proxy))
print(numeric_proxy - 3, numeric_proxy * 3)
print(-numeric_proxy, ~numeric_proxy)

class ProxyOperatorBox:
    def __floordiv__(self, value):
        return 42
    def __ifloordiv__(self, value):
        return 21
    def __matmul__(self, value):
        return 1729
    def __rmatmul__(self, value):
        return -163
    def __imatmul__(self, value):
        return 561
    def __index__(self):
        return 10

operator_box = ProxyOperatorBox()
operator_proxy = weakref.proxy(operator_box)
print(operator_proxy // 5, operator_proxy @ 5, 5 @ operator_proxy, operator.index(operator_proxy))
operator_proxy //= 5
print(operator_proxy)
operator_proxy = weakref.proxy(operator_box)
operator_proxy @= 5
print(operator_proxy)

class CallableProxyOperatorBox(ProxyOperatorBox):
    def __call__(self):
        return None

callable_operator_proxy = weakref.proxy(CallableProxyOperatorBox())
print(callable_operator_proxy // 5, callable_operator_proxy @ 5, 5 @ callable_operator_proxy, operator.index(callable_operator_proxy))

def proxy_generator():
    yield 12

generator_proxy = weakref.proxy(proxy_generator())
print(next(generator_proxy))
try:
    next(weakref.proxy(lambda: None))
except TypeError as exc:
    print(str(exc))
