import weakref
import _weakref


class Box:
    pass


b = Box()
b.name = "box"

r = weakref.ref(b)
print(r().name)
print(weakref.proxy(b).name)
print(weakref.getweakrefcount(b))
print(weakref.getweakrefs(b))

r2 = _weakref.ref(b)
print(r2().name)
print(weakref.ReferenceType(b)().name)
