import weakref
import _weakref


class Box:
    pass


b = Box()
b.name = "box"

r = weakref.ref(b)
print(r().name)
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

class CallableBox:
    def __call__(self, value):
        return value + 1

callable_box = CallableBox()
callable_proxy = weakref.proxy(callable_box)
print(callable_proxy(4), type(callable_proxy).__name__)

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
