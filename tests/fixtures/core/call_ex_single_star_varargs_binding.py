"""Exact one-star varargs binding must match CPython and keep the fallback."""


def collect(*args):
    return args


tuple_input = (1, 2, 3)
tuple_result = collect(*tuple_input)
print(tuple_result, tuple_result is tuple_input)

list_input = [4, 5, 6]
list_result = collect(*list_input)
print(list_result, list_result is list_input)


def collect_with_default(*args, marker=17):
    return args, marker


print(collect_with_default(*(7, 8)))


class TupleSubclass(tuple):
    pass


print(collect(*TupleSubclass((9, 10))))

events = []


def generate_once():
    events.append("iterated")
    yield 11
    yield 12


print(collect(*generate_once()), events)
