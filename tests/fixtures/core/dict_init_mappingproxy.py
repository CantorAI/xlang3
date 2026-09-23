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
