from pydantic_core import ArgsKwargs


for value in (ArgsKwargs(()), ArgsKwargs((), {}), ArgsKwargs((1,), {'x': 2})):
    print(repr(value), repr(value.kwargs))


class ReprEqual:
    def __eq__(self, other):
        return repr(other) == 'ArgsKwargs(())'


empty = ArgsKwargs(())
comparator = ReprEqual()
print('reflected', empty.__eq__(comparator) is NotImplemented, empty == comparator)
