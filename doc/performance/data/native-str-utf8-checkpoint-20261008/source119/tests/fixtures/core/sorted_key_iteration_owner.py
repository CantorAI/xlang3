"""Additional strict sorted key lifetime before collecting user iterables.

The existing seven-group scoped-entry fixture and expected bytes stay unchanged.
No benchmark, library replacement or implementation-specific behavior is used.
"""
import gc


def make_key(registry, released, seen):
    class Token:
        def __del__(self):
            released.append('released')

    token = Token()

    def key(value):
        assert token is not None and not released
        seen.append(value)
        return value

    registry.append(key)


def iterator_removal():
    registry, released, seen = [], [], []
    make_key(registry, released, seen)

    class Items:
        def __iter__(self):
            registry.clear()
            gc.collect()
            assert not released
            return iter([2, 1])

    assert sorted(Items(), key=registry[0]) == [1, 2]
    assert seen == [2, 1]
    gc.collect()
    assert released == ['released']


def next_removal():
    registry, released, seen = [], [], []
    make_key(registry, released, seen)

    class Items:
        def __init__(self):
            self.index = 0

        def __iter__(self):
            return self

        def __next__(self):
            if self.index == 2:
                raise StopIteration
            if self.index == 0:
                registry.clear()
                gc.collect()
                assert not released
            self.index += 1
            return 3 - self.index

    assert sorted(Items(), key=registry[0]) == [1, 2]
    assert seen == [2, 1]
    gc.collect()
    assert released == ['released']


def iterator_failure():
    registry, released, seen = [], [], []
    make_key(registry, released, seen)
    marker = LookupError('iteration marker')

    class Items:
        def __iter__(self):
            registry.clear()
            gc.collect()
            assert not released
            raise marker

    try:
        sorted(Items(), key=registry[0])
    except LookupError as caught:
        assert caught is marker
        names = []
        traceback = caught.__traceback__
        while traceback is not None:
            names.append(traceback.tb_frame.f_code.co_name)
            traceback = traceback.tb_next
        assert '__iter__' in names
    else:
        raise AssertionError('original iterator failure was lost')
    assert seen == []
    gc.collect()
    assert released == ['released']


def comparisons_and_key_cleanup():
    registry, released, seen, compared, cleaned = [], [], [], [], []

    class OrderKey:
        def __init__(self, value):
            self.value = value

        def __lt__(self, other):
            compared.append((self.value, other.value))
            gc.collect()
            assert not released
            return self.value < other.value

        def __del__(self):
            cleaned.append(not released)

    class Token:
        def __del__(self):
            released.append('released')

    def install_key():
        token = Token()

        def key(value):
            assert token is not None and not released
            registry.clear()
            seen.append(value)
            return OrderKey(value)

        registry.append(key)

    install_key()
    assert sorted([2, 1], key=registry[0]) == [1, 2]
    assert seen == [2, 1] and compared == [(1, 2)]
    gc.collect()
    assert cleaned == [True, True]
    assert released == ['released']


for label, case in [
    ('key-owned-before-iter', iterator_removal),
    ('key-owned-before-next', next_removal),
    ('key-owned-on-iteration-failure', iterator_failure),
    ('key-owned-through-comparison-cleanup', comparisons_and_key_cleanup),
]:
    case()
    print('PASS ' + label)
