import sys
import weakref
import builtins


# VM cache storage must remain correct when a warm function reaches new sites,
# monitoring callbacks disable/restart events, or an activation drops its owner.
# Keep this fixture when changing cache allocation or cleanup for performance.
class Item:
    def __init__(self, value):
        self.value = value

    @property
    def doubled(self):
        return self.value * 2

    def add(self, amount):
        return self.value + amount


def exercise(late_branch):
    total = 0
    for index in range(12):
        item = Item(index)
        total += item.add(2)
        if late_branch:
            total += item.doubled + len([index])
    return total


for _ in range(20):
    assert exercise(False) == 90
print("late-sites", exercise(False), exercise(True))
Item.add = lambda self, amount: self.value - amount
print("method-mutation", exercise(False), exercise(True))

monitor = sys.monitoring
tool = 5
monitor.use_tool_id(tool, "cache-materialization")
observed = []


def instruction_event(code, offset):
    observed.append(code is exercise.__code__)
    return monitor.DISABLE


monitor.register_callback(tool, monitor.events.INSTRUCTION, instruction_event)
monitor.set_local_events(tool, exercise.__code__, monitor.events.INSTRUCTION)
assert exercise(True) == 186
print("monitor-late-sites", bool(observed), all(observed))
observed.clear()
monitor.restart_events()
assert exercise(False) == 42
print("monitor-restart", bool(observed), all(observed))
monitor.set_local_events(tool, exercise.__code__, 0)
monitor.register_callback(tool, monitor.events.INSTRUCTION, None)
monitor.free_tool_id(tool)


class Owner:
    def __init__(self, value):
        self.value = value


def cached_owner():
    owner = Owner(17)
    reference = weakref.ref(owner)
    for _ in range(12):
        assert owner.value == 17
    return reference


print("owner-released", cached_owner()() is None)


def scalar_sites(sequence, index):
    return len(sequence), sequence[index]


# Accumulate specialization across short activations, then change operand
# shapes, indices, and Python special methods at the same IR cache sites.
for _ in range(24):
    assert scalar_sites(b"abc", 1) == (3, 98)
unicode_result = scalar_sites("é日", 1)
print("scalar-shapes", scalar_sites([7, 8], -1), scalar_sites((4, 5), 0),
      (unicode_result[0], ord(unicode_result[1])), scalar_sites(bytearray(b"ab"), -1))


class Indexed:
    def __len__(self):
        return 4

    def __getitem__(self, index):
        return index + 10


indexed = Indexed()
print("scalar-method", scalar_sites(indexed, 2))
Indexed.__getitem__ = lambda self, index: index + 20
Indexed.__len__ = lambda self: 5
print("scalar-method-mutation", scalar_sites(indexed, 2))
for sequence, index in ((b"abc", 8), ([], 0), (None, 0)):
    try:
        scalar_sites(sequence, index)
    except (IndexError, TypeError) as error:
        print("scalar-error", type(error).__name__)


class BuiltinTarget:
    def __call__(self):
        return 73


def fused_builtin_call():
    return cache_fixture_builtin()


def builtin_call_owner():
    target = BuiltinTarget()
    reference = weakref.ref(target)
    builtins.cache_fixture_builtin = target
    del target
    assert fused_builtin_call() == 73
    del builtins.cache_fixture_builtin
    return reference


print("fused-global-call-owner-released", builtin_call_owner()() is None)


class BuiltinContainer:
    def __init__(self):
        self.value = 89


def fused_builtin_attribute():
    return cache_fixture_container.value


def builtin_attribute_owner():
    container = BuiltinContainer()
    reference = weakref.ref(container)
    builtins.cache_fixture_container = container
    del container
    assert fused_builtin_attribute() == 89
    del builtins.cache_fixture_container
    return reference


print("fused-global-attr-owner-released", builtin_attribute_owner()() is None)
