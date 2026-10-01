calls = []


class Key:
    def __init__(self, name):
        self.name = name

    def __hash__(self):
        calls.append(self.name)
        return 19

    def __eq__(self, other):
        return self is other


first = Key("first")
second = Key("second")
values = {first}
values.add(second)
print(calls, len(values))
calls.clear()
print(second in values, calls)
calls.clear()
values.discard(first)
print(calls, len(values))


class MutatingKey:
    def __init__(self):
        self.calls = 0

    def __hash__(self):
        self.calls += 1
        if self.calls > 1:
            mutating_values.clear()
        return 23


mutating_first = MutatingKey()
mutating_values = {mutating_first}
mutating_values.add(Key("third"))
print(mutating_first.calls, len(mutating_values))


# Warm the large-set membership index before every mutation. Its cached vector
# positions must be rebuilt even when a mutation keeps the same set size.
def warm_values():
    values = set(range(16))
    assert 15 in values
    assert 99 not in values
    return values


values = warm_values()
values.remove(15)
values.discard(14)
print("erase", 15 in values, 14 in values, 13 in values, len(values))
values.add(31)
print("add", 31 in values, len(values))

values = warm_values()
removed = values.pop()
print("pop", removed not in values, len(values))

values = warm_values()
values.update(range(8, 24))
print("update", 23 in values, len(values))

values = warm_values()
values.intersection_update(range(8))
print("intersection", 7 in values, 8 in values, len(values))

values = warm_values()
values.difference_update(range(8))
print("difference", 0 in values, 15 in values, len(values))

values = warm_values()
values.symmetric_difference_update(range(8, 24))
print("symmetric", 7 in values, 15 in values, 23 in values, len(values))

values = warm_values()
values.__init__(range(20, 36))
print("reinitialize", 15 in values, 35 in values, len(values))
values.clear()
print("clear", 35 in values, len(values))


class MutatingEquality:
    armed = False
    replace = False

    def __hash__(self):
        return 7

    def __eq__(self, other):
        if self.armed:
            self.armed = False
            self.owner.clear()
            if self.replace:
                self.owner.add(other)
        return isinstance(other, MutatingEquality)


# CPython restarts a probe when equality changes the entry/table. Exercise both
# the VM's `in` operation and the native set.__contains__ method on a warm index.
for use_method in (False, True):
    for replace in (False, True):
        stored = MutatingEquality()
        queried = MutatingEquality()
        values = set(range(100, 116))
        values.add(stored)
        assert 115 in values
        stored.owner = values
        stored.replace = replace
        stored.armed = True
        found = values.__contains__(queried) if use_method else queried in values
        print("eq-mutation", use_method, replace, found, len(values))


class CollisionKey:
    def __init__(self, number):
        self.number = number

    def __hash__(self):
        return 3

    def __eq__(self, other):
        return isinstance(other, CollisionKey) and self.number == other.number


values = {CollisionKey(number) for number in range(16)}
print("collisions", CollisionKey(15) in values, CollisionKey(99) in values)
values.remove(CollisionKey(15))
print("collision-remove", CollisionKey(15) in values, len(values))
frozen = frozenset(values)
print("frozen", CollisionKey(14) in frozen, CollisionKey(99) in frozen)
