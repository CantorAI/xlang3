"""Native min/max streaming, error propagation and owner lifetime regression."""
events = []


class Rank:
    def __init__(self, value):
        self.value = value

    def __gt__(self, other):
        events.append(('compare', self.value, other.value))
        return self.value > other.value

    def __lt__(self, other):
        events.append(('compare', self.value, other.value))
        return self.value < other.value


def source():
    for value in (1, 2, 3):
        events.append(('yield', value))
        yield value


def rank(value):
    events.append(('key', value))
    return Rank(value)


for operation, expected in ((min, 1), (max, 3)):
    events.clear()
    assert operation(source(), key=rank) == expected
    assert events == [('yield', 1), ('key', 1), ('yield', 2), ('key', 2),
                      ('compare', 2, 1), ('yield', 3), ('key', 3),
                      ('compare', 3, 1 if operation is min else 2)]
print('streaming iteration, key and comparison order: OK')


def failing_key(value):
    events.append(('key', value))
    if value == 2:
        raise LookupError('key failed')
    return value


for operation in (min, max):
    events.clear()
    try:
        operation(source(), key=failing_key)
    except LookupError as error:
        assert str(error) == 'key failed'
    else:
        raise AssertionError('key error lost')
    assert events == [('yield', 1), ('key', 1), ('yield', 2), ('key', 2)]
print('key errors stop iteration immediately: OK')


class BadComparison:
    def __gt__(self, other):
        raise ArithmeticError('compare failed')

    def __lt__(self, other):
        raise ArithmeticError('compare failed')


class BadTruth:
    def __bool__(self):
        raise ArithmeticError('truth failed')


class ComparisonTruth:
    def __gt__(self, other):
        return BadTruth()

    def __lt__(self, other):
        return BadTruth()


for operation in (min, max):
    for comparison_type, message in ((BadComparison, 'compare failed'),
                                     (ComparisonTruth, 'truth failed')):
        events.clear()
        try:
            operation(source(), key=lambda value: comparison_type())
        except ArithmeticError as error:
            assert str(error) == message
        else:
            raise AssertionError('comparison error lost')
        assert events == [('yield', 1), ('yield', 2)]
print('comparison and truth errors stop iteration immediately: OK')


class FailingIterator:
    def __iter__(self):
        raise OSError('iter failed')


class FailingNext:
    def __iter__(self):
        return self

    def __next__(self):
        raise OSError('next failed')


for operation in (min, max):
    for iterable, message in ((FailingIterator(), 'iter failed'),
                              (FailingNext(), 'next failed')):
        try:
            operation(iterable, default='unused')
        except OSError as error:
            assert str(error) == message
        else:
            raise AssertionError('iterator error lost')
print('iterator exceptions survive default handling: OK')


for operation in (min, max):
    items = [1, 2]

    def grow(value):
        if value == 1:
            items.append(3)
        return value

    assert operation(items, key=grow) == (1 if operation is min else 3)
    assert items == [1, 2, 3]
    first, second = object(), object()
    assert operation([first, second], key=lambda value: 1) is first
    assert operation([], default=first, key=42) is first
    assert operation([3, 1, 2], key=None) == (1 if operation is min else 3)
    assert operation(3, 1, 2) == (1 if operation is min else 3)
    try:
        operation(1, 2, default=0)
    except TypeError:
        pass
    else:
        raise AssertionError('multiple positional default accepted')
    try:
        operation([])
    except ValueError:
        pass
    else:
        raise AssertionError('empty iterable accepted')
print('mutation, identity, ties, defaults and argument forms: OK')


class Item:
    def __init__(self, value):
        self.value = value

    def __del__(self):
        events.append(('item_del', self.value))


class DisposableRank(Rank):
    def __del__(self):
        events.append(('key_del', self.value))


class Items:
    def __init__(self):
        self.position = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.position += 1
        if self.position > 3:
            events.append(('end',))
            raise StopIteration
        events.append(('yield', self.position))
        return Item(self.position)


def disposable_key(item):
    events.append(('key', item.value))
    return DisposableRank(item.value)


for operation in (min, max):
    events.clear()
    result = operation(Items(), key=disposable_key)
    assert result.value == (1 if operation is min else 3)
    expected = [('yield', 1), ('key', 1)]
    for value in (2, 3):
        old = 1 if operation is min else value - 1
        expected.extend([('yield', value), ('key', value), ('compare', value, old)])
        if operation is min:
            expected.extend([('item_del', value), ('key_del', value)])
        else:
            expected.extend([('key_del', old), ('item_del', old)])
    expected.extend([('end',), ('key_del', result.value)])
    assert events == expected, events
    value = result.value
    del result
    assert events == expected + [('item_del', value)], events
print('discarded and replaced owners released before advancing: OK')
