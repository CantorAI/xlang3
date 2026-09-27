calls = []


def make_type():
    calls.append(1)
    return int


type Lazy = make_type()
print("lazy", type(Lazy).__name__, len(calls), Lazy.__module__, repr(Lazy))
print("value", Lazy.__value__ is int, len(calls), Lazy.__value__ is int, len(calls))


def local_alias():
    target = str
    type Local = target
    return Local


print("local", local_alias().__value__ is str)


class Container:
    target = bytes
    type Nested = target


print("class", Container.Nested.__value__ is bytes)
type Generic[T] = list[T]
print("generic", type(Generic.__type_params__[0]).__name__, repr(Generic.__value__), repr(Generic[int]))
type Recursive = list[Recursive]
print("recursive", repr(Recursive.__value__))
