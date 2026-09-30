def define():
    class Example:
        first = 1
        middle = 2
        last = 3

    namespace = vars(Example)
    print(type(namespace).__name__)
    print([name for name in namespace if name in ('first', 'middle', 'last')])
    print([name for name, value in namespace.items() if name in ('first', 'middle', 'last')])
    Example.middle = 4
    print(namespace['middle'])


define()
