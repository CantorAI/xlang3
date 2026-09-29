from functools import lru_cache


class Key:
    def __init__(self, value):
        self.value = value
        self.hash_calls = 0
        self.equal_calls = 0

    def __hash__(self):
        self.hash_calls += 1
        return 1

    def __eq__(self, other):
        self.equal_calls += 1
        return isinstance(other, Key) and self.value == other.value


calls = 0


@lru_cache(maxsize=4)
def combine(left, right):
    global calls
    calls += 1
    return left.value + right.value


left = Key(10)
right = Key(20)
print(combine(left, right), combine(left, right), calls,
      left.hash_calls, right.hash_calls, left.equal_calls, right.equal_calls)

equal_left = Key(10)
equal_right = Key(20)
print(combine(equal_left, equal_right), calls,
      left.equal_calls, right.equal_calls)
print(combine(Key(11), Key(21)), calls)


@lru_cache(maxsize=4)
def with_keyword(left, *, right):
    return left + right


print(with_keyword(2, right=3), with_keyword(2, right=3))
