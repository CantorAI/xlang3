"""Native dict.get transports original Python hash/equality exceptions."""
import sys
import traceback

cause = ValueError('cause')
failure = LookupError('key not found')
failure.add_note('original dict callback')


class HashKey:
    def __hash__(self):
        raise failure from cause


table = {}
saved = table.get
for mode in range(3):
    try:
        raise RuntimeError('outer')
    except RuntimeError as outer:
        try:
            if mode == 0:
                table.get(HashKey(), 17)
            elif mode == 1:
                saved(HashKey(), 17)
            else:
                saved(*(HashKey(), 17))
        except LookupError as caught:
            assert caught is failure and caught.__cause__ is cause
            assert caught.__notes__ == ['original dict callback']
            assert '__hash__' in [frame.name for frame in traceback.extract_tb(caught.__traceback__)]
        else:
            raise AssertionError('hash failure became a default or different exception')
        assert sys.exception() is outer
print('dict get direct, bound and expanded hash failures preserve identity and traceback: OK')


class EqualityFailure(BaseException):
    pass


equality_failure = EqualityFailure('equality marker')


class StoredKey:
    def __hash__(self):
        return 71

    def __eq__(self, other):
        raise equality_failure


class QueryKey:
    def __hash__(self):
        return 71


collisions = {StoredKey(): 23}
try:
    collisions.get(QueryKey(), 29)
except EqualityFailure as caught:
    assert caught is equality_failure
    assert '__eq__' in [frame.name for frame in traceback.extract_tb(caught.__traceback__)]
else:
    raise AssertionError('equality failure became a default or different exception')


def intrinsic_query(receiver, key):
    return receiver.get(key, 29)


for query in ('stored', 7):
    calls = []

    class CollisionKey:
        def __hash__(self):
            return hash(query)

        def __eq__(self, other):
            calls.append(other)
            raise equality_failure

    collision_table = {CollisionKey(): 23}
    try:
        intrinsic_query(collision_table, query)
    except EqualityFailure as caught:
        assert caught is equality_failure and calls == [query]
    else:
        raise AssertionError('exact query skipped stored Python equality')


integer_calls = []


class IntegerKey(int):
    def __hash__(self):
        return int.__hash__(self)

    def __eq__(self, other):
        integer_calls.append(other)
        raise equality_failure


integer_table = {IntegerKey(7): 23}
try:
    intrinsic_query(integer_table, 7)
except EqualityFailure as caught:
    assert caught is equality_failure and integer_calls == [7]
else:
    raise AssertionError('integer payload skipped stored subclass equality')
print('dict get preserves non-Exception equality failures: OK')


for operation in (lambda: table.get([]), lambda: table.get()):
    try:
        operation()
    except TypeError:
        pass
    else:
        raise AssertionError('dict TypeError fallback was lost')
default = []
assert table.get('absent', default) is default and table.get('absent') is None
table['present'] = 31
assert table.get('present', default) == 31
print('dict get retains TypeError fallback, misses and successful recovery: OK')
