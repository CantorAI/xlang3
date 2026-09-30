from contextlib import contextmanager


@contextmanager
def values():
    yield (3, 4)


class Example:
    with values() as (first, second):
        total = first + second
        if True:
            label = 'ready'

        def describe(self):
            return self.total, self.label


print(Example.first, Example.second, Example().describe())
