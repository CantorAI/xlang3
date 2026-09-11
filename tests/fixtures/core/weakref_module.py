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
print(weakref.getweakrefcount(b))
print(weakref.getweakrefs(b))

r2 = _weakref.ref(b)
print(r2().name)
print(weakref.ReferenceType(b)().name)
print(weakref.ref(b) == weakref.ref(b), weakref.ref(b) != weakref.ref(Box()))

expired = weakref.ref(Box())
import gc
gc.collect()
print(expired() is None)
events = []
callback_ref = weakref.ref(Box(), lambda ref: events.append(ref() is None))
gc.collect()
print(events)
proxy_events = []
callback_proxy = weakref.proxy(Box(), lambda ref: proxy_events.append(ref is callback_proxy))
gc.collect()
print(proxy_events)
