from collections import Counter, OrderedDict, defaultdict, deque


class CCounter(Counter[int]):
    pass


class CDefaultDict(defaultdict[str, int]):
    pass


class CDeque(deque[int]):
    pass


class CDict(dict[str, int]):
    pass


class CFrozenset(frozenset[int]):
    pass


class CList(list[int]):
    pass


class COrderedDict(OrderedDict[str, int]):
    pass


class CSet(set[int]):
    pass


class CTuple(tuple[int]):
    pass


for klass, value in (
    (CCounter, Counter([1, 2])),
    (CDefaultDict, (None, {'a': 1})),
    (CDeque, deque([1, 2])),
    (CDict, {'a': 1}),
    (CFrozenset, frozenset([1, 2])),
    (CList, [1, 2]),
    (COrderedDict, OrderedDict([('a', 1)])),
    (CSet, {1, 2}),
    (CTuple, (1, 2)),
):
    result = klass(*value) if klass is CDefaultDict else klass(value)
    print(klass.__name__, len(result))
