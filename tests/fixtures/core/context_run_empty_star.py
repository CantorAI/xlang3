import contextvars


def callback(*args):
    print("callback", args)


context = contextvars.copy_context()
context.run(callback, *())
context.run(callback, *[])
context.run(callback, *("from tuple",))
context.run(callback, *["from list"])


class Iterable:
    def __iter__(self):
        print("iterated")
        return iter(())


context.run(callback, *Iterable())


class EmptyList(list):
    def __iter__(self):
        print("list iterated")
        return iter(())


class EmptyTuple(tuple):
    def __iter__(self):
        print("tuple iterated")
        return iter(())


context.run(callback, *EmptyList())
context.run(callback, *EmptyTuple())
