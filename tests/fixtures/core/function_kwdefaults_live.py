import gc
import types
import weakref


def required(call):
    try:
        call()
    except TypeError:
        return
    raise AssertionError("deleted keyword default was still used")


class LazyDecorator:
    def __call__(self, original):
        # NetworkX copies positional defaults, then supplies the wrapper's
        # self-reference through a mangled keyword-only default.
        def wrapped(*args, __wrapper=None, **kwargs):
            return __wrapper.__argmap__

        wrapped.__defaults__ = original.__defaults__
        wrapped.__kwdefaults__["_LazyDecorator__wrapper"] = wrapped
        wrapped.__argmap__ = self
        return wrapped


def original():
    return None


decorator = LazyDecorator()
assert decorator(original)() is decorator


def wrapper(*args, target=None, **kwargs):
    return target


defaults = wrapper.__kwdefaults__
assert defaults is wrapper.__kwdefaults__
defaults["target"] = wrapper
assert wrapper() is wrapper
assert wrapper(*(), **{}) is wrapper
defaults.clear()
required(wrapper)

wrapper.__kwdefaults__ = {"target": "delete attribute"}
del wrapper.__kwdefaults__
assert wrapper.__kwdefaults__ is None
required(wrapper)
replacement = {"target": "updated"}
wrapper.__kwdefaults__ = replacement
assert wrapper.__kwdefaults__ is replacement
replacement["target"] = "changed again"
assert wrapper() == "changed again"
del replacement["target"]
required(wrapper)
replacement["target"] = "restored"
assert wrapper() == "restored"
wrapper.__kwdefaults__ = None
assert wrapper.__kwdefaults__ is None
required(wrapper)


def added(*, target):
    return target


added.__kwdefaults__ = replacement
assert added() == "restored"
wrapper.__kwdefaults__ = replacement
replacement["target"] = 42
assert wrapper() == added() == 42
try:
    added.__kwdefaults__ = []
except TypeError:
    pass
else:
    raise AssertionError("invalid defaults assignment accepted")
assert added.__kwdefaults__ is replacement
for name in ("__defaults__", "__kwdefaults__"):
    try:
        setattr(added, name, [])
    except TypeError:
        pass
    else:
        raise AssertionError("setattr accepted invalid function defaults")
assert added.__kwdefaults__ is replacement
replacement[17] = "ignored non-string key"
assert added() == 42


def mixed(pos=1, *, kw=2):
    return pos, kw


# Clearing positional defaults must preserve unexposed keyword defaults too.
mixed.__defaults__ = None
assert mixed(3) == (3, 2)
live = mixed.__kwdefaults__
mixed.__defaults__ = (10,)
live["kw"] = 20
assert mixed() == (10, 20)
mixed.__defaults__ = None
assert mixed(30) == (30, 20)
mixed.__defaults__ = (40,)
del mixed.__defaults__
assert mixed(50) == (50, 20)


class Box:
    def method(self, *, target=None):
        return target


Box.method.__kwdefaults__["target"] = "method default"
box = Box()
assert box.method() == "method default"
assert box.method(*(), **{}) == "method default"
bound = box.method
assert bound() == "method default"
Box.method.__kwdefaults__.clear()
required(bound)


def generate(*, target=1):
    yield target


generate.__kwdefaults__["target"] = "before call"
pending = generate()
generate.__kwdefaults__["target"] = "after call"
assert next(pending) == "before call"
assert next(generate()) == "after call"


async def coroutine(*, target=1):
    return target


coroutine.__kwdefaults__["target"] = "coroutine creation"
pending = coroutine()
coroutine.__kwdefaults__["target"] = "later mutation"
try:
    next(pending.__await__())
except StopIteration as done:
    assert done.value == "coroutine creation"
else:
    raise AssertionError("coroutine did not finish")


class Configured:
    def __init__(self, *, target=1):
        self.target = target

    def getter(self, *, target=2):
        return target

    value = property(getter)


Configured.__init__.__kwdefaults__["target"] = "constructor default"
Configured.getter.__kwdefaults__["target"] = "property default"
assert Configured().target == "constructor default"
assert Configured(*(), **{}).target == "constructor default"
assert Configured().value == "property default"


class Defaults(dict):
    def __getitem__(self, key):
        raise AssertionError("binding invoked subclass __getitem__")


subclass_defaults = Defaults(target="subclass")
added.__kwdefaults__ = subclass_defaults
assert added.__kwdefaults__ is subclass_defaults
assert added() == "subclass"
subclass_defaults["target"] = "mutated subclass"
assert added() == "mutated subclass"


clone_defaults = {"target": "constructor"}
clone = types.FunctionType(added.__code__, globals(), kwdefaults=clone_defaults)
assert clone.__kwdefaults__ is clone_defaults
clone_defaults["target"] = "constructor mutation"
assert clone() == "constructor mutation"


class Payload:
    pass


def make_default():
    payload = Payload()
    reference = weakref.ref(payload)

    def hold(*, target=payload):
        return target

    return hold, reference


hold, reference = make_default()
hold.__kwdefaults__.clear()
assert reference() is None, "stale indexed default retained the payload"
hold, reference = make_default()
hold.__kwdefaults__ = None
assert reference() is None, "defaults assignment retained the old payload"


def make_cycle(include_attribute=False):
    def self_default(*, target=None):
        return target

    self_default.__kwdefaults__["target"] = self_default
    if include_attribute:
        self_default.alias = self_default
    assert self_default() is self_default
    return weakref.ref(self_default)


for include_attribute in (False, True):
    reference = make_cycle(include_attribute)
    gc.collect()
    assert reference() is None, "keyword-default cycle was not traversed"

# NetworkX copies _dispatchable.__dict__ into an argmap function. Like CPython
# data descriptors, native function defaults must beat the copied shadow keys.
def dictionary_shadow(value=3, *, target=4):
    return value, target

live_defaults = dictionary_shadow.__kwdefaults__
dictionary_shadow.__dict__.update({"__defaults__": (99,), "__kwdefaults__": {"target": 99}})
assert dictionary_shadow.__defaults__ == (3,)
assert dictionary_shadow.__kwdefaults__ is live_defaults
dictionary_shadow.__kwdefaults__["target"] = 5
assert dictionary_shadow() == (3, 5)
assert dictionary_shadow.__dict__["__kwdefaults__"]["target"] == 99

print("function live keyword defaults ok")
