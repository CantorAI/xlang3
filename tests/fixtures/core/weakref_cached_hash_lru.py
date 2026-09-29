import weakref
from functools import lru_cache


class Target:
    def __init__(self):
        self.hash_calls = 0

    def __hash__(self):
        self.hash_calls += 1
        return 271


@lru_cache(maxsize=None)
def cached(ref):
    return "cached"


target = Target()
ref = weakref.ref(target)
first_hash = hash(ref)
print(cached(ref), cached(ref), target.hash_calls)
del target
print(hash(ref) == first_hash)


class Unhashable:
    __hash__ = None


unhashable = Unhashable()
unhashable_ref = weakref.ref(unhashable)
try:
    hash(unhashable_ref)
except TypeError:
    print("unhashable")
