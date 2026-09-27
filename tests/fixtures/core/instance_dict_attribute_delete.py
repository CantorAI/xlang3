from functools import cached_property


class Counter:
    def __init__(self):
        self.calls = 0

    @cached_property
    def value(self):
        self.calls += 1
        return self.calls


counter = Counter()
print(counter.value, counter.value)
del counter.value
print(counter.value, counter.calls)
counter.__dict__['extra'] = 7
del counter.extra
print('extra' in counter.__dict__)
