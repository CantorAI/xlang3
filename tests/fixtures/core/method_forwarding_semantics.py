# These wrappers exercise dynamic method lookup and traceback frames. A VM
# forwarding fast path must preserve subclass overrides and fall back through
# the Python method whenever the forwarded operation can raise.
class Collector:
    def __init__(self):
        self.items = []

    def append(self, item):
        self.items.append(item)


def invoke(collector, item):
    return collector.append(item)


collector = Collector()
print(invoke(collector, 3))
print(collector.items)


def append_many(collector):
    for item in range(8):
        collector.append(item)


collector = Collector()
print(append_many(collector))
print(collector.items)


class ForeignItems:
    def __init__(self):
        self.calls = []

    def append(self, item):
        self.calls.append(item)
        return "foreign result"


foreign = ForeignItems()
collector.items = foreign
print(invoke(collector, 4))
print(foreign.calls)


def replacement(self, item):
    self.items.append(item * 10)


Collector.append = replacement
collector.items = []
print(invoke(collector, 5))
print(collector.items)

collector = Collector()
collector.append = lambda item: collector.items.append(item + 100)
print(invoke(collector, 6))
print(collector.items)


class CustomList(list):
    def __init__(self):
        self.calls = []

    def append(self, item):
        self.calls.append(item * 10)


class CustomCollector:
    def __init__(self):
        self.items = CustomList()

    def append(self, item):
        self.items.append(item)


custom = CustomCollector()
custom.append(7)
print(custom.items.calls)
print(len(custom.items))


class PopCollector:
    def __init__(self):
        self.items = []

    def pop(self, index):
        return self.items.pop(index)


def invoke_pop(collector):
    return collector.pop(4)


try:
    invoke_pop(PopCollector())
except Exception as error:
    import traceback
    print(type(error).__name__)
    print([frame.name for frame in traceback.extract_tb(error.__traceback__)])
