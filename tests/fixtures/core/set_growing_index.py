# Interleave membership with insertion, as graph visited sets do. Every growth
# boundary, clear, and removal must preserve the authoritative ordered entries.
values = ["node-" + str(index) for index in range(2048)]
seen = set()
for value in values:
    assert value not in seen
    seen.add(value)
    assert value in seen
    seen.add(value)
assert len(seen) == len(values)
assert set(values) == seen
assert frozenset(values) == seen
for value in values[::3]:
    seen.remove(value)
    assert value not in seen
    seen.add(value)
assert seen == set(values)
seen.clear()
seen.update(values)
assert len(seen) == len(values)
assert all(value in seen for value in values)
assert {1, 1.0, True} == {1}


class Collision:
    def __init__(self, value):
        self.value = value

    def __hash__(self):
        return 42

    def __eq__(self, other):
        return isinstance(other, Collision) and self.value == other.value


collisions = set(Collision(index) for index in range(32))
for index in range(32):
    collisions.add(Collision(index))
    assert Collision(index) in collisions
assert len(collisions) == 32


target = set(range(20))


class Mutating:
    def __init__(self, active):
        self.active = active
        self.hash_calls = 0

    def __hash__(self):
        self.hash_calls += 1
        return 5000

    def __eq__(self, other):
        if self.active:
            self.active = False
            target.clear()
            return False
        return isinstance(other, Mutating)


original = Mutating(True)
target.add(original)
query = Mutating(False)
target.add(query)
assert query.hash_calls == 1, "insertion recomputed a Python hash on restart"
assert len(target) == 1 and next(iter(target)) is query


class TruthMutation:
    def __bool__(self):
        target.clear()
        return False


class TruthKey(Mutating):
    __hash__ = Mutating.__hash__

    def __eq__(self, other):
        if self.active:
            self.active = False
            return TruthMutation()
        return isinstance(other, TruthKey)


target = set(range(20))
target.add(TruthKey(True))
query = TruthKey(False)
target.add(query)
assert query.hash_calls == 1
assert len(target) == 1 and next(iter(target)) is query


class TrueMutation(Mutating):
    __hash__ = Mutating.__hash__

    def __eq__(self, other):
        target.clear()
        return True


target = set(range(20))
target.add(TrueMutation(True))
query = TrueMutation(False)
target.add(query)
assert query.hash_calls == 1
assert len(target) == 0, "successful equality must finish insertion after callback mutation"
print("growing set index and callback mutation ok")
