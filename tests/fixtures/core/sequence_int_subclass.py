from enum import Enum, IntEnum


class Index(IntEnum):
    FIRST = 0
    LAST = 2


class NotAnIndex(Enum):
    FIRST = 0


values = [10, 20, 30]
print(values[Index.LAST], (10, 20, 30)[Index.FIRST])
values[Index.FIRST] = 11
print(values)
del values[Index.LAST]
print(values)
try:
    values[NotAnIndex.FIRST]
except TypeError as exc:
    print(type(exc).__name__)
