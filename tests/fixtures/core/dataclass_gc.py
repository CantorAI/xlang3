import dataclasses
import gc
import weakref


def make_local_dataclass():
    @dataclasses.dataclass
    class LocalDataclass:
        value: int

    return LocalDataclass


local_dataclass = make_local_dataclass()
local_dataclass_ref = weakref.ref(local_dataclass)
del local_dataclass
gc.collect()
print(local_dataclass_ref() is None)
