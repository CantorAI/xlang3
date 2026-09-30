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
