import gc
import weakref


class Holder:
    value = 42


unrelated_targets = [Holder() for _ in range(1000)]
unrelated_refs = [weakref.ref(item) for item in unrelated_targets]
target = Holder()
basic = weakref.ref(target)
proxy = weakref.proxy(target)
assert weakref.ref(target) is basic
assert weakref.proxy(target) is proxy
assert basic() is target and proxy.value == 42
assert weakref.getweakrefcount(target) == 2
assert {id(item) for item in weakref.getweakrefs(target)} == {id(basic), id(proxy)}
print("indexed ref reuse and target enumeration", True)

callbacks = []
refs = [weakref.ref(target, lambda ref, index=index: callbacks.append((index, ref() is None)))
        for index in range(4)]
assert weakref.getweakrefcount(target) == 6
assert weakref.ref(target) is basic
assert hash(basic) == hash(target)
del target
gc.collect()
assert basic() is None and all(ref() is None for ref in refs)
assert callbacks == [(3, True), (2, True), (1, True), (0, True)]
assert isinstance(hash(basic), int)
try:
    proxy.value
except ReferenceError:
    pass
else:
    raise AssertionError("dead proxy remained alive")
print("invalidation and callback order", True)


class DerivedRef(weakref.ref):
    pass


other = Holder()
subref = DerivedRef(other)
other_basic = weakref.ref(other)
assert subref is not other_basic
assert weakref.ref(other) is other_basic
assert subref() is other
assert weakref.getweakrefcount(other) == 2
del other
gc.collect()
assert subref() is None and other_basic() is None
try:
    hash(subref)
except TypeError:
    pass
else:
    raise AssertionError("unhashed dead reference accepted")
print("reference subclasses and dead hash", True)

for index in range(256):
    instance = Holder()
    ref = weakref.ref(instance)
    assert ref() is instance
    assert weakref.ref(instance) is ref
    del instance
    gc.collect()
    assert ref() is None
assert all(ref() is target for ref, target in zip(unrelated_refs, unrelated_targets))
del ref
assert len(unrelated_refs) == 1000
print("repeated lifetime and unrelated references", True)
