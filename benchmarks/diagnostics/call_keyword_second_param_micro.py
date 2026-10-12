"""Expose the one-positional, named-second-parameter Python call binder path."""


def invoke(first, *, second):
    return second


def main():
    result = None
    for index in range(250_000):
        result = invoke(index, second=7)
    print(result)


main()
