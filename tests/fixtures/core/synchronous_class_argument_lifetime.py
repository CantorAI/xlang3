"""Synchronous module-method class arguments must not outlive their call.

No constructor for Ephemeral is invoked: the target is the weakref class call
itself, independently of canonical-constructor optimization eligibility.
"""

import gc
import weakref


def creator(check_active):
    class Ephemeral:
        __slots__ = ("obj",)

        def __init__(self, obj):
            self.obj = obj

    class_ref = weakref.ref(Ephemeral)
    initializer_ref = weakref.ref(Ephemeral.__init__)
    del Ephemeral
    gc.collect()
    assert class_ref() is None
    if check_active:
        assert initializer_ref() is None
    return class_ref, initializer_ref


class_ref, initializer_ref = creator(False)
gc.collect()
assert class_ref() is None and initializer_ref() is None
print("returned creator releases referents")

class_ref, initializer_ref = creator(True)
assert class_ref() is None and initializer_ref() is None
print("synchronous class call retires argument copies")
