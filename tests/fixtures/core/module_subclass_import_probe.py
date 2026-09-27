import importlib
import sys
import types


class Item:
    value = 42


class ForwardingModule(types.ModuleType):
    def __init__(self, source):
        super().__init__(source.__name__)
        self.source = source

    def __getattr__(self, name):
        return getattr(self.source, name)


source = types.ModuleType("probe_module")
source.Item = Item
wrapped = ForwardingModule(source)
sys.modules["probe_module"] = wrapped

try:
    importlib.import_module("probe_module.child")
except ModuleNotFoundError as exc:
    print(type(exc).__name__, exc.name)

print(wrapped.Item().value)
