import gc

events = []


class Parent:
    pass


class Child(Parent):
    pass


def drop(value):
    return None


drop(Child())
gc.collect()


def finalize(self):
    events.append("finalized")


Parent.__del__ = finalize
drop(Child())
gc.collect()
assert events == ["finalized"]
print("class finalizer cache invalidated")
