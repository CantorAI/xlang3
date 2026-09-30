from collections.abc import Iterable, Iterator


for value in (map(str, (1,)), filter(None, (1,)), zip((1,), (2,)), enumerate((1,))):
    print(type(value).__name__, isinstance(value, Iterable), isinstance(value, Iterator),
          type(value).__iter__(value) is value, next(value))
