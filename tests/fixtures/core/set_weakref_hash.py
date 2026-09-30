import gc
import weakref


class Key:
    def __init__(self):
        self.hash_calls = 0

    def __hash__(self):
        self.hash_calls += 1
        return 42


key = Key()
values = set()
values.add(key)
print(key.hash_calls, len(values))

key = Key()
values = set([key])
print(key.hash_calls, len(values))

key = Key()
values = {key}
print(key.hash_calls, len(values))


class Item:
    pass


def make_weakset():
    item = Item()
    reference = weakref.ref(item)
    entries = weakref.WeakSet([item])
    return reference, entries


reference, entries = make_weakset()
print(reference() is None, len(entries))
gc.collect()
print(reference() is None, len(entries))

item = Item()
reference = weakref.ref(item)
values = set()
values.add(reference)
cached_hash = hash(reference)
del item
gc.collect()
print(reference() is None, hash(reference) == cached_hash)


class BadHash:
    def __hash__(self):
        raise ValueError("hash failed")


bad = BadHash()
for create in (
    lambda: set().add(bad),
    lambda: set([bad]),
    lambda: {bad},
    lambda: {item for item in [bad]},
):
    try:
        create()
    except Exception as error:
        print(type(error).__name__, str(error))
