import types


class Wrapped(types.ModuleType):
    def __init__(self, module):
        super().__init__(module.__name__)
        self.source = module


source = types.ModuleType("source")
wrapped = Wrapped(source)
print(type(wrapped).__name__, wrapped.__name__, wrapped.source is source)

try:
    types.ModuleType(source)
except Exception as exc:
    print(type(exc).__name__, str(exc))

try:
    types.ModuleType.__new__(Wrapped, source)
except Exception as exc:
    print(type(exc).__name__, str(exc))
else:
    print("subclass new accepted module")

fresh = types.ModuleType.__new__(types.ModuleType, "raw")
print("new dict", fresh.__dict__)


class Counting(types.ModuleType):
    def __init__(self):
        super().__init__("counting")
        self.lookups = 0

    def __getattr__(self, name):
        self.lookups += 1
        raise AttributeError(name)


counting = Counting()
try:
    counting.missing
except AttributeError as exc:
    print("missing", str(exc), counting.lookups)
