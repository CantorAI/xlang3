"""Strict proposed sorted integer/fallback contract; not a timing workload."""


def integer_shapes():
    cases = [
        ([], []),
        ([7], [7]),
        ([-3, -1, 0, 4], [-3, -1, 0, 4]),
        ([4, 0, -1, -3], [-3, -1, 0, 4]),
        ([4, -3, 4, 0, -3, -1], [-3, -3, -1, 0, 4, 4]),
        ([-9223372036854775808, 9223372036854775807, 0],
         [-9223372036854775808, 0, 9223372036854775807]),
    ]
    for values, expected in cases:
        before = values[:]
        result = sorted(values)
        assert result == expected and values == before and result is not values
        assert sorted(values, reverse=True) == expected[::-1]
    assert sorted(range(31, -1, -1)) == list(range(32))


def stable_integer_keys():
    values = [(2, 'a'), (1, 'b'), (2, 'c'), (1, 'd'), (3, 'e')]
    assert sorted(values, key=lambda item: item[0]) == [
        values[1], values[3], values[0], values[2], values[4]]
    assert sorted(values, key=lambda item: item[0], reverse=True) == [
        values[4], values[0], values[2], values[1], values[3]]
    # Opposite-direction equal groups must never take the whole-run reversal.
    ordered = [(3, 'a'), (3, 'b'), (2, 'c'), (2, 'd'), (1, 'e')]
    assert sorted(ordered, key=lambda item: item[0]) == [
        ordered[4], ordered[2], ordered[3], ordered[0], ordered[1]]
    assert sorted(ordered, key=lambda item: item[0], reverse=True) == ordered
    assert sorted(values, key=lambda item: 0) == values
    assert sorted(values, key=lambda item: 0, reverse=True) == values


def callback_order_and_snapshot():
    events = []
    values = [3, 1, 2]

    def key(value):
        events.append(value)
        if value == 3:
            values.append(99)
        return value

    assert sorted(values, key=key) == [1, 2, 3]
    assert events == [3, 1, 2] and values == [3, 1, 2, 99]
    events.clear()
    assert sorted([], key=key) == [] and not events
    assert sorted([2], key=key) == [2] and events == [2]
    assert sorted([2, 1], key=None) == [1, 2]


def iteration_protocol():
    events = []

    class ListOverride(list):
        def __iter__(self):
            events.append('iter')
            return iter([3, 1, 2])

    def key(value):
        events.append(value)
        return value

    source = ListOverride([20, 10])
    assert sorted(source, key=key) == [1, 2, 3]
    assert events == ['iter', 3, 1, 2] and source[0] == 20
    assert sorted(iter([3, 2, 1]), reverse=True) == [3, 2, 1]


def original_failures():
    marker = LookupError('exact sorted key marker')
    seen = []

    def key(value):
        seen.append(value)
        if value == 2:
            raise marker
        return value

    try:
        sorted([1, 2, 3], key=key)
    except LookupError as caught:
        assert caught is marker
    else:
        raise AssertionError('key failure was lost')
    assert seen == [1, 2]
    iter_marker = ValueError('exact sorted iterator marker')

    class Broken:
        def __iter__(self):
            raise iter_marker

    try:
        sorted(Broken())
    except ValueError as caught:
        assert caught is iter_marker
    else:
        raise AssertionError('iterator failure was lost')


def numeric_fallbacks():
    huge = 2 ** 80
    assert sorted([huge, -huge, 2, 1.5, False, True]) == [
        -huge, False, True, 1.5, 2, huge]
    assert sorted([1.5, -2.0, 0.0]) == [-2.0, 0.0, 1.5]
    assert sorted([True, False, True]) == [False, True, True]
    calls = []

    class ReverseInt(int):
        def __lt__(self, other):
            calls.append(1)
            return int.__lt__(other, self)

    a, b, c = ReverseInt(1), ReverseInt(3), ReverseInt(2)
    result = sorted([a, b, c])
    assert result[0] is b and result[1] is c and result[2] is a and calls


def user_comparison_fallback():
    calls = []

    class Key:
        def __init__(self, rank):
            self.rank = rank

        def __lt__(self, other):
            calls.append((self.rank, other.rank))
            return self.rank < other.rank

    values = [(2, 'a'), (1, 'b'), (2, 'c')]
    result = sorted(values, key=lambda item: Key(item[0]))
    assert result == [values[1], values[0], values[2]] and calls
    calls.clear()
    result = sorted(values, key=lambda item: Key(item[0]), reverse=True)
    assert result == [values[0], values[2], values[1]] and calls


for label, case in [
    ('integer-shapes', integer_shapes),
    ('stable-integer-keys', stable_integer_keys),
    ('callback-order-and-snapshot', callback_order_and_snapshot),
    ('iteration-protocol', iteration_protocol),
    ('original-failures', original_failures),
    ('numeric-fallbacks', numeric_fallbacks),
    ('user-comparison-fallback', user_comparison_fallback),
]:
    case()
    print('sorted-exact-int', label, 'PASS')
