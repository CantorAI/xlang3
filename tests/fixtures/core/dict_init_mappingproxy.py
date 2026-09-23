class Source:
    answer = 42


class DictSubclass(dict):
    def __new__(cls, *args):
        new = dict.__new__(cls)
        dict.__init__(new, *args)
        return new


plain = {}
dict.__init__(plain, Source.__dict__)
subclass = DictSubclass(Source.__dict__)
print(plain["answer"], subclass["answer"])


class WrappedGetitem(dict):
    def wrap(func):
        def inner(self, key):
            return func(self, key)

        return inner

    __getitem__ = wrap(dict.__getitem__)
    del wrap


wrapped = WrappedGetitem(answer=42)
print(wrapped["answer"], dict.__getitem__(wrapped, "answer"))


events = []


class PopOverride(dict):
    def __getitem__(self, key):
        events.append(("get", key))
        raise AssertionError("dict.pop must bypass __getitem__")

    def __delitem__(self, key):
        events.append(("del", key))
        raise AssertionError("dict.pop must bypass __delitem__")


values = PopOverride(present=7)
print(values.pop(".", None), values.pop("present"), events, len(values))
