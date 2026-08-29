# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# warnings facade: filters accepted and catch_warnings(record=True) captures warn().
import warnings
import _warnings

warnings.simplefilter("always")
with warnings.catch_warnings(record=True) as seen:
    warnings.warn("hello")
    _warnings.warn("native")

print(len(seen), seen[0].message, seen[0].category.__name__)
print(seen[1].message)
warnings.resetwarnings()

# functools facade: wraps/update_wrapper propagate common metadata and partial binds prefix args.
import functools
import fnmatch
import glob
import code
from contextlib import contextmanager

def original(a, b):
    "doc text"
    return a + b

@functools.wraps(original)
def wrapper(a, b):
    return original(a, b)

print(wrapper.__name__, wrapper.__doc__, wrapper.__wrapped__ is original, wrapper(2, 3))

def plain():
    pass

functools.update_wrapper(plain, original)
print(plain.__name__, plain.__doc__, plain.__wrapped__ is original)
add_two = functools.partial(original, 2)
print(add_two(5))
print(add_two.func is original, add_two.args, add_two.keywords is None)

# contextlib.contextmanager preserves Python function metadata and writable wrapper docs.
@contextmanager
def managed_value():
    "managed doc"
    yield "ok"

print(managed_value.__name__, managed_value.__doc__, managed_value.__wrapped__.__name__)
managed_value.__doc__ = "changed doc"
print(managed_value.__doc__)

def combine(a, b):
    return a * 10 + b

def cmp_num(a, b):
    return a - b

Key = functools.cmp_to_key(cmp_num)
print(functools.reduce(combine, [1, 2, 3]), functools.reduce(combine, [2, 3], 1))
print(Key(1) < Key(2), Key(2) > Key(1), Key(2) == Key(2), Key(3) != Key(2))

# functools cache decorators memoize positional calls, expose info/clear helpers, and enforce bounded LRU eviction.
cache_calls = []

@functools.lru_cache(maxsize=2)
def cached_double(value):
    cache_calls.append(value)
    return value * 2

print(cached_double(1), cached_double(1), cached_double(2), cached_double(3), cached_double(1))
print(cache_calls, cached_double.cache_info())
cached_double.cache_clear()
print(cached_double.cache_info())

@functools.cache
def cached_inc(value):
    cache_calls.append(value + 100)
    return value + 1

print(cached_inc(5), cached_inc(5), cached_inc.cache_info(), cached_inc.cache_parameters())
compiled_command = code.compile_command("answer = 42")
print(compiled_command.co_filename, compiled_command.co_name, code.compile_command("if True:") is None)
print(fnmatch.fnmatch("alpha.py", "*.py"), fnmatch.fnmatchcase("alpha.py", "a[!0-9]*.py"))
print(fnmatch.filter(["a.py", "b.txt", "c.py"], "*.py"))
print(fnmatch.filterfalse(["a.py", "b.txt", "c.py"], "*.py"))
print(glob.has_magic("*.py"), glob.escape("a*[b]?"))

@functools.total_ordering
class OrderedValue:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        return self.value < other.value

    def __eq__(self, other):
        return self.value == other.value

left_ordered = OrderedValue(2)
right_ordered = OrderedValue(3)
same_ordered = OrderedValue(2)
print(left_ordered <= right_ordered, right_ordered > left_ordered, left_ordered >= same_ordered)

# string public constants are available for libraries that avoid importing _string directly.
import string

print(string.ascii_lowercase[:3], string.ascii_uppercase[-3:], string.digits, "A" in string.hexdigits)
print(len(string.octdigits), "\n" in string.whitespace, callable(string.Formatter))
formatter = string.Formatter()
print(formatter.format("{}-{}", "fmt", 7), formatter.format_field(9, "03d"))
print(list(formatter.parse("a{name!r:>4}b"))[0])
print(formatter.get_value(1, ("x", "y"), {}))

# dataclasses: annotated fields generate init/repr/eq metadata foundations.
import dataclasses

@dataclasses.dataclass
class Point:
    x: int
    y: int = 5

p = Point(2)
q = Point(2, y=5)
print(p.x, p.y, q.y, len(Point.__dataclass_fields__), Point.__dataclass_fields__["x"].name)
print(p.__repr__())
print(p.__eq__(q))
print(dataclasses.is_dataclass(Point), dataclasses.is_dataclass(p), dataclasses.fields(Point)[0].name, dataclasses.asdict(p)["y"])

# contextlib: generator context managers and nullcontext work with with-statements.
import contextlib

@contextlib.contextmanager
def cm():
    yield "ctx"

with cm() as value:
    print(value)

with contextlib.nullcontext("null") as value:
    print(value)

# contextlib: closing calls close() on exit and suppress swallows selected exceptions.
class CloseTarget:
    def __init__(self):
        self.closed = False

    def close(self):
        self.closed = True

target = CloseTarget()
with contextlib.closing(target) as item:
    print(item is target, target.closed)
print(target.closed)

with contextlib.suppress(ValueError):
    raise ValueError("hidden")
print("suppressed")

# contextlib: ExitStack closes callbacks and entered contexts in LIFO order.
events = []

class StackTarget:
    def __enter__(self):
        events.append("enter")
        return "stacked"

    def __exit__(self, exc_type, exc, tb):
        events.append("exit")
        return False

def stack_callback(label):
    events.append(label)

with contextlib.ExitStack() as stack:
    print(stack.enter_context(StackTarget()))
    stack.callback(stack_callback, "callback")

print(events)

# types/copy: singleton type aliases must point at real runtime singleton types.
import types
import copy
import sys

print(types.NoneType is type(None), types.NotImplementedType is type(NotImplemented), types.EllipsisType is type(...), ... is Ellipsis, type(...).__name__, type(...).__module__)
print("copy-singletons", copy.copy(None) is None, copy.copy(...) is Ellipsis, copy.deepcopy(NotImplemented) is NotImplemented)
def types_function_probe():
    return None

types_generator_probe = (value for value in [1])
def types_cell_outer():
    value = "cell"
    def types_cell_inner():
        return value
    return types_cell_inner.__closure__[0]

types_cell_probe = types_cell_outer()
try:
    raise RuntimeError("types-traceback")
except RuntimeError as types_traceback_error:
    types_traceback_probe = types_traceback_error.__traceback__
print(
    "types-runtime-aliases",
    types.FunctionType is type(types_function_probe),
    types.LambdaType is types.FunctionType,
    types.CodeType is type(types_function_probe.__code__),
    types.FrameType is type(sys._getframe()),
    types.TracebackType is type(types_traceback_probe),
    types.GeneratorType is type(types_generator_probe),
    types.CellType is type(types_cell_probe),
    types.BuiltinFunctionType is type(len),
    types.BuiltinMethodType is type([].append),
    type(len).__name__ == "builtin_function_or_method",
    type([].append).__name__ == "builtin_function_or_method",
)

# argparse: common parser shape with option aliases, typed values, flags, and positional args.
import argparse
import ast

parser = argparse.ArgumentParser()
parser.add_argument("-n", "--num", type=int, default=1)
parser.add_argument("--verbose", action="store_true")
parser.add_argument("name")
parsed = parser.parse_args(["--num", "7", "--verbose", "bob"])
print(parsed.num, parsed.verbose, parsed.name)
parser2 = argparse.ArgumentParser(prog="tool", description="demo parser", add_help=False)
parser2.add_argument("--off", action="store_false", dest="enabled")
parser2.add_argument("--mode", choices=["fast", "slow"], required=True)
parser2.add_argument("--tag", action="append", default=[])
parser2.add_argument("-v", action="count")
parser2.add_argument("--const", action="store_const", const="C", default="D")
parser2.add_argument("--pair", nargs=2)
parser2.add_argument("--scale", type=float, default=1.0)
ns = argparse.Namespace(existing="keep")
parsed2 = parser2.parse_args(["--mode", "fast", "--tag", "a", "--tag", "b", "-v", "-v", "--off", "--const", "--pair", "x", "y", "--scale=2.5"], namespace=ns)
print(isinstance(parsed2, argparse.Namespace), parsed2.existing, parsed2.mode, parsed2.tag, parsed2.v, parsed2.enabled, parsed2.const, parsed2.pair, parsed2.scale)
known, unknown = parser2.parse_known_args(["--mode", "slow", "--unknown", "value"])
print(known.mode, unknown, "usage: tool" in parser2.format_usage(), "demo parser" in parser2.format_help())
try:
    parser2.parse_args(["--mode", "bad"])
except Exception as exc:
    print("argparse-error", "invalid choice" in str(exc))

# ast: constructible nodes, field iteration, dumping, walking, and literal_eval foundations.
const_node = ast.Constant(9)
bin_node = ast.BinOp(left=const_node, op=ast.Add(), right=ast.Constant(value=4))
module_node = ast.Module(body=[ast.Expr(value=bin_node)], type_ignores=[])
print(ast.literal_eval(const_node), list(ast.iter_fields(bin_node))[0][0], len(list(ast.walk(module_node))))
print(ast.dump(bin_node))
print(isinstance(ast.parse("x = 1"), ast.Module), ast.parse("1", mode="eval").__class__.__name__)

class NameCollector(ast.NodeVisitor):
    def __init__(self):
        self.names = []

    def visit_Name(self, node):
        self.names.append(node.id)

collector = NameCollector()
collector.visit(ast.Module(body=[ast.Expr(value=ast.Name(id="seen", ctx=ast.Load()))], type_ignores=[]))
print(collector.names)

# abc/_abc: native ABCMeta register/check helpers feed normal isinstance/issubclass.
import abc
import _abc
import weakref

print(abc.ABCMeta.__module__, abc.ABC.__module__, abc.abstractmethod.__module__, _abc.get_cache_token.__module__, abc.ABCMeta.__qualname__, abc.ABC.__qualname__, abc.ABCMeta.__doc__ is not None, abc.ABC.__doc__ is not None)
print("abc-class-docs", abc.ABCMeta.__doc__.startswith("Metaclass for defining Abstract Base Classes (ABCs)."), "virtual subclasses" in abc.ABCMeta.__doc__, abc.ABCMeta.__doc__.endswith("\n"), abc.ABC.__doc__ == "Helper class that provides a standard way to create an ABC using\ninheritance.\n")
print("abc-function-docs", abc.abstractmethod.__doc__.startswith("A decorator indicating abstract methods.\n\nRequires"), "my_abstract_method" in abc.abstractmethod.__doc__, "opaque object" in abc.get_cache_token.__doc__, abc.update_abstractmethods.__doc__.endswith("does nothing.\n"), "Deprecated, use 'classmethod'" in abc.abstractclassmethod.__doc__, "Deprecated, use 'staticmethod'" in abc.abstractstaticmethod.__doc__, "Deprecated, use 'property'" in abc.abstractproperty.__doc__)
print("abc-meta-method-docs", abc.ABCMeta.register.__doc__.endswith("usage as a class decorator.\n"), abc.ABCMeta.__instancecheck__.__doc__ == "Override for isinstance(instance, cls).", abc.ABCMeta.__subclasscheck__.__doc__ == "Override for issubclass(subclass, cls).")
print(abc.abstractmethod.__name__, abc.ABCMeta.register.__name__, _abc._abc_init.__name__, abc.abstractmethod.__doc__ is not None, _abc.__doc__ is not None)

class NativeABC(metaclass=abc.ABCMeta):
    pass

class Concrete:
    pass

token_before = abc.get_cache_token()
print(NativeABC.register(Concrete) is Concrete, abc.get_cache_token() > token_before)
print(issubclass(Concrete, NativeABC), isinstance(Concrete(), NativeABC))
print(len(_abc._get_dump(NativeABC)[0]) >= 1, _abc._abc_subclasscheck(NativeABC, Concrete), _abc._abc_instancecheck(NativeABC, Concrete()))
native_dump = _abc._get_dump(NativeABC)
print(len(native_dump[1]) == 0, len(native_dump[2]) == 0, native_dump[3] == abc.get_cache_token())
native_registry_ref = next(iter(native_dump[0]))
print(native_registry_ref() is Concrete, Concrete in native_dump[0], native_registry_ref in native_dump[0])
print(type(native_registry_ref).__name__, type(native_registry_ref).__module__, weakref.ReferenceType is type(native_registry_ref))
print(hasattr(native_registry_ref, "__xlang3_weakref_target__"), hasattr(native_registry_ref, "__xlang3_weakref_target_ptr__"))
print(weakref.getweakrefcount(Concrete) >= 1, native_registry_ref in weakref.getweakrefs(Concrete))
native_weakref_count = weakref.getweakrefcount(Concrete)
native_dump_again = _abc._get_dump(NativeABC)
native_registry_ref_again = next(iter(native_dump_again[0]))
print(weakref.getweakrefcount(Concrete) == native_weakref_count, native_registry_ref_again is native_registry_ref, native_registry_ref_again() is Concrete)
_abc._reset_registry(NativeABC)
print(issubclass(Concrete, NativeABC), _abc._abc_subclasscheck(NativeABC, Concrete))
native_dump = _abc._get_dump(NativeABC)
print(len(native_dump[0]), len(native_dump[1]), len(native_dump[2]) >= 1)
_abc._reset_caches(NativeABC)
native_dump = _abc._get_dump(NativeABC)
print(len(native_dump[0]), len(native_dump[1]), len(native_dump[2]), native_dump[3] == abc.get_cache_token())
print(NotImplemented is NotImplemented, type(NotImplemented).__name__)

class HookedABC(metaclass=abc.ABCMeta):
    @classmethod
    def __subclasshook__(cls, subclass):
        if cls is HookedABC and hasattr(subclass, "hook_marker"):
            return True
        return NotImplemented

class MarkedConcrete:
    hook_marker = True

class PlainConcrete:
    pass

print(issubclass(MarkedConcrete, HookedABC), isinstance(MarkedConcrete(), HookedABC), issubclass(PlainConcrete, HookedABC))
hooked_dump = _abc._get_dump(HookedABC)
print(len(hooked_dump[1]) >= 1, len(hooked_dump[2]) >= 1)

class RejectingABC(metaclass=abc.ABCMeta):
    @classmethod
    def __subclasshook__(cls, subclass):
        return False

RejectingABC.register(Concrete)
print(issubclass(Concrete, RejectingABC), isinstance(Concrete(), RejectingABC), _abc._abc_subclasscheck(RejectingABC, Concrete))
rejecting_dump = _abc._get_dump(RejectingABC)
print(len(rejecting_dump[0]) >= 1, len(rejecting_dump[2]) >= 1)

class LaterRegisteredABC(metaclass=abc.ABCMeta):
    pass

class LaterConcrete:
    pass

print(issubclass(LaterConcrete, LaterRegisteredABC))
later_dump = _abc._get_dump(LaterRegisteredABC)
print(len(later_dump[2]) >= 1, later_dump[3] == abc.get_cache_token())
LaterRegisteredABC.register(LaterConcrete)
later_dump = _abc._get_dump(LaterRegisteredABC)
print(len(later_dump[2]) >= 1, later_dump[3] < abc.get_cache_token())
print(issubclass(LaterConcrete, LaterRegisteredABC))
later_dump = _abc._get_dump(LaterRegisteredABC)
print(len(later_dump[2]), later_dump[3] == abc.get_cache_token())

class DirectABC(metaclass=abc.ABCMeta):
    pass

class DirectConcrete(DirectABC):
    pass

direct_token = abc.get_cache_token()
print(DirectABC.register(DirectConcrete) is DirectConcrete, abc.get_cache_token() == direct_token, len(_abc._get_dump(DirectABC)[0]))
try:
    DirectConcrete.register(DirectABC)
except RuntimeError as err:
    print("abc-cycle", "inheritance cycle" in str(err))
print(issubclass(DirectConcrete, DirectABC), len(_abc._get_dump(DirectABC)[1]) >= 1)
direct_reset_token = abc.get_cache_token()
print(_abc._reset_registry(DirectABC) is None, abc.get_cache_token() == direct_reset_token, len(_abc._get_dump(DirectABC)[0]), len(_abc._get_dump(DirectABC)[1]) >= 1, issubclass(DirectConcrete, DirectABC))

class DirectRejectingABC(metaclass=abc.ABCMeta):
    @classmethod
    def __subclasshook__(cls, subclass):
        return False

class DirectRejectedConcrete(DirectRejectingABC):
    pass

print(issubclass(DirectRejectedConcrete, DirectRejectingABC), isinstance(DirectRejectedConcrete(), DirectRejectingABC), _abc._abc_subclasscheck(DirectRejectingABC, DirectRejectedConcrete))
direct_rejecting_dump = _abc._get_dump(DirectRejectingABC)
print(len(direct_rejecting_dump[2]) >= 1)

@abc.abstractmethod
def abstract_fn():
    pass

print(abstract_fn.__isabstractmethod__)
def abstract_keyword_fn():
    pass
def abstract_keyword_classmethod(cls):
    return cls.__name__
def abstract_keyword_staticmethod():
    return "static-keyword"
def abstract_keyword_property(self):
    return "property-keyword"
def abstract_keyword_setter(self, value):
    pass
def abstract_keyword_deleter(self):
    pass
def abstract_doc_property(self):
    "getter doc"
    return "doc-property"
def abstract_doc_replacement(self):
    "replacement doc"
    return "replacement-property"
abstract_keyword_result = abc.abstractmethod(funcobj=abstract_keyword_fn)
abstract_classmethod_result = abc.abstractclassmethod(callable=abstract_keyword_classmethod)
abstract_staticmethod_result = abc.abstractstaticmethod(callable=abstract_keyword_staticmethod)
abstract_property_result = abc.abstractproperty(fget=abstract_keyword_property)
abstract_empty_property_result = abc.abstractproperty()
abstract_setter_property_result = abc.abstractproperty(fset=abstract_keyword_setter)
abstract_deleter_property_result = abc.abstractproperty(fdel=abstract_keyword_deleter)
abstract_full_property_result = abc.abstractproperty(abstract_keyword_property, abstract_keyword_setter, abstract_keyword_deleter, "abstract doc")
abstract_doc_property_result = abc.abstractproperty(abstract_doc_property)
abstract_doc_getter_result = abstract_doc_property_result.getter(abstract_doc_replacement)
abstract_doc_explicit_result = abc.abstractproperty(abstract_doc_property, None, None, "explicit doc").getter(abstract_doc_replacement)
abstract_doc_chain_result = abstract_doc_property_result.setter(abstract_keyword_setter).getter(abstract_doc_replacement)
abstract_name_property_result = abc.abstractproperty(abstract_doc_property)
abstract_name_property_result.__name__ = "manual_name"
abstract_name_chain_result = abstract_name_property_result.setter(abstract_keyword_setter).getter(abstract_doc_replacement)
del abstract_name_property_result.__name__
abstract_empty_name_result = abc.abstractproperty()
abstract_empty_name_missing = hasattr(abstract_empty_name_result, "__name__")
abstract_empty_name_result.__name__ = "empty_name"
print("abc-decorator-keyword", abstract_keyword_result is abstract_keyword_fn, abstract_keyword_fn.__isabstractmethod__, type(abstract_classmethod_result).__name__, abstract_classmethod_result.__isabstractmethod__, abstract_classmethod_result.__func__.__isabstractmethod__)
print("abc-decorator-keyword", type(abstract_staticmethod_result).__name__, abstract_staticmethod_result.__isabstractmethod__, abstract_staticmethod_result.__func__.__isabstractmethod__, type(abstract_property_result).__name__, abstract_property_result.__isabstractmethod__, getattr(abstract_property_result.fget, "__isabstractmethod__", "missing"), type(abstract_empty_property_result).__name__, abstract_empty_property_result.__isabstractmethod__)
print("abc-abstractproperty-marker", abstract_setter_property_result.__isabstractmethod__, abstract_deleter_property_result.__isabstractmethod__, abstract_full_property_result.__isabstractmethod__, abstract_full_property_result.__doc__)
print("abc-abstractproperty-doc", abstract_doc_property_result.__doc__, abstract_doc_getter_result.__doc__, abstract_doc_explicit_result.__doc__, abstract_doc_chain_result.__doc__)
print("abc-abstractproperty-name", abc.abstractproperty(abstract_doc_property).__name__, abstract_doc_chain_result.__name__, abstract_name_chain_result.__name__, abstract_name_property_result.__name__, abstract_empty_name_missing, abstract_empty_name_result.__name__)
property_diagnostic_cases = (
    ("getter-missing", lambda: property.getter(), "unbound method property.getter() needs an argument"),
    ("getter-extra", lambda: abstract_empty_property_result.getter(abstract_keyword_property, abstract_keyword_property), "property.getter() takes exactly one argument (2 given)"),
    ("getter-keyword", lambda: abstract_empty_property_result.getter(fget=abstract_keyword_property), "property.getter() takes no keyword arguments"),
    ("getter-receiver", lambda: property.getter(42, abstract_keyword_property), "descriptor 'getter' for 'property' objects doesn't apply to a 'int' object"),
    ("setter-missing", lambda: property.setter(), "unbound method property.setter() needs an argument"),
    ("setter-extra", lambda: abstract_empty_property_result.setter(abstract_keyword_setter, abstract_keyword_setter), "property.setter() takes exactly one argument (2 given)"),
    ("setter-keyword", lambda: abstract_empty_property_result.setter(fset=abstract_keyword_setter), "property.setter() takes no keyword arguments"),
    ("setter-receiver", lambda: property.setter(42, abstract_keyword_setter), "descriptor 'setter' for 'property' objects doesn't apply to a 'int' object"),
    ("deleter-missing", lambda: property.deleter(), "unbound method property.deleter() needs an argument"),
    ("deleter-extra", lambda: abstract_empty_property_result.deleter(abstract_keyword_deleter, abstract_keyword_deleter), "property.deleter() takes exactly one argument (2 given)"),
    ("deleter-keyword", lambda: abstract_empty_property_result.deleter(fdel=abstract_keyword_deleter), "property.deleter() takes no keyword arguments"),
    ("deleter-receiver", lambda: property.deleter(42, abstract_keyword_deleter), "descriptor 'deleter' for 'property' objects doesn't apply to a 'int' object"),
)
for property_diagnostic_name, property_diagnostic_call, property_diagnostic_message in property_diagnostic_cases:
    try:
        property_diagnostic_call()
    except TypeError as err:
        print("abc-abstractproperty-method-diagnostic", property_diagnostic_name, str(err) == property_diagnostic_message)
for abstract_target in (
    42,
    property(lambda self: 1),
    object(),
    len,
    staticmethod(lambda: 1),
    classmethod(lambda cls: 1),
):
    try:
        abc.abstractmethod(abstract_target)
    except AttributeError as err:
        print("abc-abstractmethod-attr", "__isabstractmethod__" in str(err) or "attribute assignment" in str(err))
for abstract_builtin_type in (type, object, int, str, Exception, BaseException):
    try:
        abc.abstractmethod(abstract_builtin_type)
    except TypeError as err:
        print("abc-abstractmethod-immutable-type", abstract_builtin_type.__name__ in str(err), "immutable type" in str(err))

class AbstractDescriptorProbe:
    @abc.abstractclassmethod
    def named(cls):
        return cls.__name__

    @abc.abstractstaticmethod
    def static():
        return "static"

    @abc.abstractproperty
    def prop(self):
        return "prop"

raw_abstract_class = AbstractDescriptorProbe.__dict__["named"]
raw_abstract_static = AbstractDescriptorProbe.__dict__["static"]
raw_abstract_property = AbstractDescriptorProbe.__dict__["prop"]
print(type(raw_abstract_class).__name__, raw_abstract_class.__isabstractmethod__, raw_abstract_class.__func__.__isabstractmethod__, AbstractDescriptorProbe.named())
print(type(raw_abstract_static).__name__, raw_abstract_static.__isabstractmethod__, raw_abstract_static.__func__.__isabstractmethod__, AbstractDescriptorProbe.static())
print(type(raw_abstract_property).__name__, raw_abstract_property.__isabstractmethod__, getattr(raw_abstract_property.fget, "__isabstractmethod__", "missing"), AbstractDescriptorProbe().prop)

class AbstractEnforcedABC(metaclass=abc.ABCMeta):
    @abc.abstractmethod
    def run(self):
        return "abstract"

print("run" in AbstractEnforcedABC.__abstractmethods__, len(AbstractEnforcedABC.__abstractmethods__), type(AbstractEnforcedABC.__abstractmethods__).__name__, hasattr(AbstractEnforcedABC.__abstractmethods__, "add"))
print(isinstance(AbstractEnforcedABC.__abstractmethods__, frozenset), hash(AbstractEnforcedABC.__abstractmethods__) == hash(frozenset({"run"})))
try:
    AbstractEnforcedABC()
except TypeError as err:
    print("abc-abstract-instantiation", "abstract class" in str(err), "run" in str(err))

class StillAbstractABC(AbstractEnforcedABC):
    pass

class ConcreteABC(AbstractEnforcedABC):
    def run(self):
        return "concrete"

print("run" in StillAbstractABC.__abstractmethods__, len(ConcreteABC.__abstractmethods__), ConcreteABC().run())
print(abc.update_abstractmethods(ConcreteABC) is ConcreteABC, len(ConcreteABC.__abstractmethods__), type(ConcreteABC.__abstractmethods__).__name__)
ConcreteABC.run = abc.abstractmethod(ConcreteABC.run)
print(abc.update_abstractmethods(ConcreteABC) is ConcreteABC, "run" in ConcreteABC.__abstractmethods__, len(ConcreteABC.__abstractmethods__), isinstance(ConcreteABC.__abstractmethods__, frozenset))
try:
    ConcreteABC()
except TypeError as err:
    print("abc-updated-abstract", "abstract class" in str(err), "run" in str(err))
def concrete_run(self):
    return "restored"
ConcreteABC.run = concrete_run
print(abc.update_abstractmethods(ConcreteABC) is ConcreteABC, len(ConcreteABC.__abstractmethods__), ConcreteABC().run())
class PlainUpdateTarget:
    pass
print(abc.update_abstractmethods(PlainUpdateTarget) is PlainUpdateTarget, hasattr(PlainUpdateTarget, "__abstractmethods__"), abc.update_abstractmethods(42))

class DirectInitABC:
    @abc.abstractmethod
    def direct(self):
        return "direct"

print(hasattr(DirectInitABC, "__abstractmethods__"))
print(_abc._abc_init(DirectInitABC), "direct" in DirectInitABC.__abstractmethods__, len(DirectInitABC.__abstractmethods__))
try:
    DirectInitABC()
except TypeError as err:
    print("abc-direct-init", "abstract class" in str(err), "direct" in str(err))
direct_init_dump = _abc._get_dump(DirectInitABC)
print(len(direct_init_dump[0]), len(direct_init_dump[1]), len(direct_init_dump[2]), direct_init_dump[3] == abc.get_cache_token())
constructed_required = abc.abstractmethod(lambda self: None)
ConstructedABC = abc.ABCMeta.__new__(abc.ABCMeta, "ConstructedABC", (), {"required": constructed_required})
print(ConstructedABC.__name__, ConstructedABC.__module__, type(ConstructedABC) is abc.ABCMeta, len(ConstructedABC.__abstractmethods__))
print(_abc._get_dump(ConstructedABC)[3] == abc.get_cache_token())
class ConstructedConcrete:
    pass
print(ConstructedABC.register(ConstructedConcrete) is ConstructedConcrete, issubclass(ConstructedConcrete, ConstructedABC), _abc._abc_instancecheck(ConstructedABC, ConstructedConcrete()))
try:
    _abc._abc_init(42)
except TypeError as err:
    print("abc-init-type", "class" in str(err))
for abc_bad_call in [
    lambda: abc.get_cache_token(1),
    lambda: _abc._abc_register(NativeABC),
    lambda: _abc._abc_subclasscheck(NativeABC),
    lambda: _abc._abc_instancecheck(NativeABC),
    lambda: _abc._get_dump(),
    lambda: _abc._reset_registry(),
    lambda: _abc._reset_caches(),
    lambda: abc.update_abstractmethods(),
    lambda: abc.abstractmethod(),
]:
    try:
        abc_bad_call()
    except TypeError as err:
        print("abc-helper-type", "expected" in str(err) or "arguments" in str(err))
abc_keyword_messages = {}
for abc_keyword_bad_name, abc_keyword_bad_call in [
    ("get_cache_token", lambda: abc.get_cache_token(x=1)),
    ("_abc_init", lambda: _abc._abc_init(self=NativeABC)),
    ("_abc_register", lambda: _abc._abc_register(self=NativeABC, subclass=Concrete)),
    ("_abc_subclasscheck", lambda: _abc._abc_subclasscheck(self=NativeABC, subclass=Concrete)),
    ("_abc_instancecheck", lambda: _abc._abc_instancecheck(self=NativeABC, instance=Concrete())),
    ("_get_dump", lambda: _abc._get_dump(self=NativeABC)),
    ("_reset_registry", lambda: _abc._reset_registry(self=NativeABC)),
    ("_reset_caches", lambda: _abc._reset_caches(self=NativeABC)),
]:
    try:
        abc_keyword_bad_call()
    except TypeError as err:
        abc_keyword_messages[abc_keyword_bad_name] = str(err)
        print("abc-helper-keyword", abc_keyword_bad_name, "takes no keyword arguments" in abc_keyword_messages[abc_keyword_bad_name])
print("abc-helper-keyword-names", all([
    "_abc.get_cache_token() takes no keyword arguments" in abc_keyword_messages.get("get_cache_token", ""),
    "_abc._abc_init() takes no keyword arguments" in abc_keyword_messages.get("_abc_init", ""),
    "_abc._abc_register() takes no keyword arguments" in abc_keyword_messages.get("_abc_register", ""),
    "_abc._abc_subclasscheck() takes no keyword arguments" in abc_keyword_messages.get("_abc_subclasscheck", ""),
    "_abc._abc_instancecheck() takes no keyword arguments" in abc_keyword_messages.get("_abc_instancecheck", ""),
    "_abc._get_dump() takes no keyword arguments" in abc_keyword_messages.get("_get_dump", ""),
    "_abc._reset_registry() takes no keyword arguments" in abc_keyword_messages.get("_reset_registry", ""),
    "_abc._reset_caches() takes no keyword arguments" in abc_keyword_messages.get("_reset_caches", ""),
]))
print("abc-update-keyword", abc.update_abstractmethods(cls=ConcreteABC) is ConcreteABC)

# numbers: numeric ABC hierarchy and virtual builtin scalar registrations.
import numbers

print(issubclass(numbers.Integral, numbers.Rational), issubclass(numbers.Real, numbers.Complex), issubclass(numbers.Complex, numbers.Number))
print(isinstance(1, numbers.Integral), isinstance(True, numbers.Integral), isinstance(1, numbers.Number))
print(isinstance(1.5, numbers.Real), isinstance(1.5, numbers.Rational), isinstance(1.5, numbers.Number))

class MyIntegral:
    pass

print(numbers.Integral.register(MyIntegral) is MyIntegral, issubclass(MyIntegral, numbers.Integral), isinstance(MyIntegral(), numbers.Number))

# typing: aliases, decorators, TypeVar, NewType, Generic, and Protocol foundations.
import typing

T = typing.TypeVar("T", int, str, bound=object, covariant=True)
UserId = typing.NewType("UserId", int)
print(T.__name__, len(T.__constraints__), T.__bound__.__name__, T.__covariant__)
print(UserId(5), UserId.__name__, UserId.__supertype__.__name__)
print(typing.cast(str, "x"), typing.List[int].__name__, typing.Optional[int].__name__)

@typing.final
class FinalClass:
    pass

class Proto(typing.Protocol):
    pass

class Box(typing.Generic[T]):
    pass

print(FinalClass.__name__, issubclass(Proto, typing.Protocol), issubclass(Box, typing.Generic), typing.TYPE_CHECKING)

# __future__: feature objects expose CPython-style metadata.
import __future__

feature = __future__.annotations
print(feature.__name__, feature.getOptionalRelease()[0], feature.getMandatoryRelease(), feature.compiler_flag)
print("annotations" in __future__.all_feature_names, __future__.CO_FUTURE_ANNOTATIONS == feature.compiler_flag, "all_feature_names" in __future__.__all__)

# enum: class constants become members, aliases reuse members, auto increments, value lookup works, and unique rejects aliases.
import enum

class Color(enum.Enum):
    RED = 1
    CRIMSON = 1
    BLUE = enum.auto()

print(Color.RED.name, Color.RED.value, Color.BLUE.name, Color.BLUE.value)
print(Color(1) is Color.RED, Color(2) is Color.BLUE, Color.CRIMSON is Color.RED)
print(list(Color), Color.__members__["CRIMSON"] is Color.RED, Color._member_names_)

class Number(enum.IntEnum):
    ONE = 1
    THREE = 3

print(Number.THREE.name, Number(3) is Number.THREE, isinstance(Number.ONE, Number))

try:
    @enum.unique
    class Bad(enum.Enum):
        A = 1
        B = 1
except ValueError:
    print("unique-error")

# enum Flag/IntFlag bitwise operations and member display.
class Perm(enum.Flag):
    READ = 1
    WRITE = 2

class Mode(enum.IntFlag):
    R = 1
    W = 2
    X = 4

perm_combo = Perm.READ | Perm.WRITE
mode_combo = Mode.R | Mode.X
print(perm_combo.name, perm_combo.value, perm_combo.__repr__(), perm_combo.__str__())
print((perm_combo & Perm.READ) is Perm.READ, (perm_combo ^ Perm.WRITE) is Perm.READ, (~Perm.READ) is Perm.WRITE)
print(mode_combo.name, mode_combo.value, isinstance(mode_combo, Mode), (mode_combo & Mode.X) is Mode.X)

# ctypes: scalar values, pointer/byref contents, buffers, simple Structure defaults, wintypes, and WinDLL facade.
import ctypes
from ctypes import wintypes

ct_value = ctypes.c_int(5)
ct_ptr = ctypes.pointer(ct_value)
ct_ref = ctypes.byref(ct_value)
print(ct_value.value, ct_ptr.contents is ct_value, ct_ref.contents is ct_value)
print(ctypes.cast(ct_ptr, ctypes.POINTER(ctypes.c_int)).contents is ct_value, ctypes.addressof(ct_value) != 0)
print(ctypes.memmove(ct_ptr, ct_ref, 1) is ct_ptr, ctypes.memset(ct_ptr, 0, 1) is ct_ptr)
print(len(ctypes.create_string_buffer(3)), len(ctypes.create_string_buffer(b"abc")))

class CPoint(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int), ("y", ctypes.c_int)]

ct_point = CPoint()
print(ct_point.x, ct_point.y, ctypes.sizeof(ct_point), ctypes.sizeof(ctypes.c_int))
print(wintypes.MAX_PATH, wintypes.DWORD is ctypes.c_uint, ctypes.windll.kernel32.OpenProcess(1, 0, 1))

# getpass/locale/sysconfig/opcode/dis/winreg: common inspection helpers and constants.
import codecs
import dis
import getpass
import http
import http.client
import io
import json
import locale
import inspect
import marshal
import opcode
import os
import pathlib
import pickle
import platform
import pkgutil
import re
import signal
import site
import stat
import subprocess
import struct
import sys
import sysconfig
import threading
import time
import tokenize
import urllib.parse
import winreg
import xmlrpc.client

print(len(getpass.getuser()) > 0, len(locale.getencoding()) > 0, locale.localeconv()["decimal_point"])
print(getpass.GetPassWarning.__name__, getpass.getpass(prompt="x", stream=None) == "", getpass.default_getpass("x") == "")
print(locale.delocalize("1,234.5"), locale.localize("1234.5"), locale.atoi("1,234"), locale.atof("1,234.5"))
print(locale.strcoll("a", "b") < 0, isinstance(locale.strxfrm("abc"), str), locale.CHAR_MAX)
old_recursion_limit = sys.getrecursionlimit()
sys.setrecursionlimit(old_recursion_limit + 1)
print(sys.getdefaultencoding(), sys.getfilesystemencoding(), sys.getfilesystemencodeerrors(), sys.getrecursionlimit() == old_recursion_limit + 1)
sys.setrecursionlimit(old_recursion_limit)
print(sys.__name__, sys.__doc__.splitlines()[0] == "This module provides access to some objects used or maintained by the", "interpreter and to functions" in sys.__doc__, callable(sys.__interactivehook__), sys.__interactivehook__() is None, callable(sys._baserepl), sys._baserepl() is None)
print("sys-module-doc", "modules -- dictionary of loaded modules" in sys.__doc__, "last_exc - the last uncaught exception" in sys.__doc__, "getsizeof() -- return the size of an object in bytes" in sys.__doc__, sys.__doc__.endswith("settrace() -- set the global debug tracing function\n"))
print(sys.__interactivehook__.__name__, sys.__interactivehook__.__qualname__, sys.__interactivehook__.__module__, sys._baserepl.__name__, sys._baserepl.__qualname__, sys._baserepl.__module__, sys.__interactivehook__.__doc__ is not None, sys._baserepl.__doc__ is not None, sys._baserepl.__text_signature__ == "($module, /)")
print("sys-hook-aliases", sys.__displayhook__ is sys.displayhook, sys.__excepthook__ is sys.excepthook, sys.__unraisablehook__ is sys.unraisablehook, sys.__breakpointhook__ is sys.breakpointhook)
for sys_startup_hook_probe in (sys.__interactivehook__, sys._baserepl):
    try:
        sys_startup_hook_probe(1)
    except TypeError as err:
        print("sys-startup-hook-args", "argument" in str(err) or "takes no arguments" in str(err))
for sys_startup_hook_keyword_name, sys_startup_hook_keyword_call, sys_startup_hook_keyword_parts in [
    ("__interactivehook__", lambda: sys.__interactivehook__(x=1), ("unexpected keyword argument", "x")),
    ("_baserepl", lambda: sys._baserepl(x=1), ("takes no keyword arguments",)),
]:
    try:
        sys_startup_hook_keyword_call()
    except TypeError as err:
        print("sys-startup-hook-keyword", sys_startup_hook_keyword_name, all(part in str(err) for part in sys_startup_hook_keyword_parts))
print(sys.intern("abc") == "abc", sys.getsizeof("abc") > 0, isinstance(sys.meta_path, list), isinstance(sys.path_hooks, list), isinstance(sys.path_importer_cache, dict))
sys_meta_path_head = sys.meta_path[:3]
print("sys-meta-path-bootstrap",
      [getattr(finder, "__name__", type(finder).__name__) for finder in sys_meta_path_head],
      [getattr(finder, "__module__", None) for finder in sys_meta_path_head],
      [repr(finder) for finder in sys_meta_path_head],
      all(hasattr(finder, "find_spec") for finder in sys_meta_path_head))
print("sys-meta-path-metatype",
      [type(finder).__name__ for finder in sys_meta_path_head],
      [type(finder).__module__ for finder in sys_meta_path_head],
      [type(finder).__qualname__ for finder in sys_meta_path_head])
class SysClassMetadataProbe:
    pass
SysTypeConstructedMetadataProbe = type("SysTypeConstructedMetadataProbe", (), {})
print("class-metadata-module-qualname",
      type.__module__, type.__qualname__,
      object.__module__, object.__qualname__,
      int.__module__, int.__qualname__,
      BaseException.__module__, BaseException.__qualname__,
      SystemExit.__module__, SystemExit.__qualname__,
      SysClassMetadataProbe.__module__, SysClassMetadataProbe.__qualname__,
      SysTypeConstructedMetadataProbe.__module__, SysTypeConstructedMetadataProbe.__qualname__)
import zipimport
print("sys-path-hooks-zipimporter",
      len(sys.path_hooks) >= 1,
      sys.path_hooks[0] is zipimport.zipimporter,
      sys.path_hooks[0].__module__,
      repr(sys.path_hooks[0]))
class SysSizeProbe:
    def __sizeof__(self):
        return 123

class SysSizeDefaultProbe:
    __sizeof__ = 42

print("sys-getsizeof-protocol-overhead", sys.getsizeof(SysSizeProbe()), sys.getsizeof(SysSizeDefaultProbe(), 99))
class SysSizeNegativeProbe:
    def __sizeof__(self):
        return -1

class SysSizeTypeErrorProbe:
    def __sizeof__(self):
        raise TypeError("bad-size")

class SysSizeTypeErrorSubclass(TypeError):
    pass

class SysSizeTypeErrorSubclassProbe:
    def __sizeof__(self):
        raise SysSizeTypeErrorSubclass("bad-subclass-size")

class SysSizeValueErrorProbe:
    def __sizeof__(self):
        raise ValueError("bad-size")

class SysSizeBoolProbe:
    def __sizeof__(self):
        return True

class SysSizeStringProbe:
    def __sizeof__(self):
        return "bad-size"

try:
    sys.getsizeof(SysSizeNegativeProbe(), 99)
except ValueError as err:
    print("sys-getsizeof-negative", ">= 0" in str(err))
try:
    sys.getsizeof(SysSizeStringProbe())
except TypeError as err:
    print("sys-getsizeof-return-type", "an integer is required" in str(err))
try:
    sys.getsizeof(SysSizeDefaultProbe())
except TypeError as err:
    print("sys-getsizeof-noncallable", "'int' object is not callable" in str(err))
print("sys-getsizeof-bool-overhead", sys.getsizeof(SysSizeBoolProbe()), sys.getsizeof(SysSizeBoolProbe(), 99))
try:
    sys.getsizeof(SysSizeTypeErrorSubclassProbe())
except SysSizeTypeErrorSubclass as err:
    print("sys-getsizeof-typeerror-subclass-reraises", type(err).__name__ == "SysSizeTypeErrorSubclass")
print("sys-getsizeof-typeerror-subclass-default", sys.getsizeof(SysSizeTypeErrorSubclassProbe(), 404))
print(sys.getsizeof(SysSizeTypeErrorProbe(), 99), sys.getsizeof(SysSizeDefaultProbe(), 101))
print(sys.getsizeof(object=SysSizeProbe()), sys.getsizeof(SysSizeDefaultProbe(), default=202), sys.getsizeof(object=SysSizeDefaultProbe(), default=303))
try:
    sys.getsizeof(SysSizeValueErrorProbe(), 99)
except ValueError as err:
    print("sys-getsizeof-reraises", "bad-size" in str(err))
try:
    sys.getsizeof()
except TypeError as err:
    print("sys-getsizeof-arity", "missing required argument" in str(err), "object" in str(err))
try:
    sys.getsizeof(1, 2, 3)
except TypeError as err:
    print("sys-getsizeof-arity", "at most 2 arguments" in str(err), "3 given" in str(err))
for sys_getsizeof_bad_name, sys_getsizeof_bad_call, sys_getsizeof_bad_parts in [
    ("keyword-missing", lambda: sys.getsizeof(default=2), ("missing required argument", "object")),
    ("keyword-extra", lambda: sys.getsizeof(object=1, default=2, other=3), ("at most 2 keyword arguments", "3 given")),
    ("keyword-duplicate-object", lambda: sys.getsizeof(1, object=2), ("given by name", "object", "position (1)")),
    ("keyword-duplicate-default", lambda: sys.getsizeof(1, 2, default=3), ("at most 2 arguments", "3 given")),
    ("keyword-unexpected", lambda: sys.getsizeof(object=1, obj=2), ("unexpected keyword", "obj")),
]:
    try:
        sys_getsizeof_bad_call()
    except TypeError as err:
        sys_getsizeof_bad_message = str(err)
        print("sys-getsizeof-keyword", sys_getsizeof_bad_name, all(part in sys_getsizeof_bad_message for part in sys_getsizeof_bad_parts))
sys_intern_prefix = "xlang"
sys_intern_dynamic = sys_intern_prefix + "3"
sys_intern_canonical = sys.intern(sys_intern_dynamic)
print(sys.intern("xlang3") is sys_intern_canonical, sys._is_interned(sys_intern_canonical), sys._is_interned(sys_intern_dynamic))
sys_intern_identifier_literal = "identifier_literal"
sys_intern_spaced_literal = "identifier literal"
sys_intern_spaced_canonical = sys.intern(sys_intern_spaced_literal)
print(sys._is_interned(sys_intern_identifier_literal), sys._is_interned(sys_intern_spaced_literal), sys_intern_spaced_canonical is sys_intern_spaced_literal, sys._is_interned(sys_intern_spaced_canonical))
unicode_interned_before = sys.getunicodeinternedsize()
sys.intern("interned-size-probe")
print(sys.getunicodeinternedsize() >= unicode_interned_before, isinstance(sys._git, tuple), len(sys._git) == 3, sys._git[0] == "CPython", sys._vpath == "", sys._home is None, sys.float_repr_style == "short")
sys_stdlib_names = sys.stdlib_module_names
print(
    "sys-stdlib-module-names",
    type(sys_stdlib_names).__name__ == "frozenset",
    isinstance(sys_stdlib_names, frozenset),
    len(sys_stdlib_names) == 297,
    all(name in sys_stdlib_names for name in ("abc", "asyncio", "collections", "concurrent", "email", "sys", "time", "zipimport")),
    "xml" in sys_stdlib_names,
    "xml.etree" not in sys_stdlib_names,
    "pip" not in sys_stdlib_names,
    not hasattr(sys_stdlib_names, "add"),
)
unicode_interned_total = sys.getunicodeinternedsize()
unicode_interned_nonimmortal = sys.getunicodeinternedsize(_only_immortal=False)
unicode_interned_immortal = sys.getunicodeinternedsize(_only_immortal=True)
unicode_interned_falsey = sys.getunicodeinternedsize(_only_immortal=[])
print("sys-getunicodeinternedsize-only-immortal", unicode_interned_nonimmortal >= unicode_interned_total, isinstance(unicode_interned_immortal, int), unicode_interned_immortal <= unicode_interned_nonimmortal, unicode_interned_falsey >= unicode_interned_nonimmortal, sys.getunicodeinternedsize.__text_signature__ == "($module, /, *, _only_immortal=False)")
try:
    sys.getunicodeinternedsize(x=1)
except TypeError as err:
    print("sys-getunicodeinternedsize-keyword", "unexpected keyword argument" in str(err), "x" in str(err))
try:
    sys.getunicodeinternedsize(True, False)
except TypeError as err:
    print("sys-getunicodeinternedsize-diagnostic two-pos", "at most 1 argument" in str(err), "2 given" in str(err))
try:
    sys.getunicodeinternedsize(_only_immortal=False, x=1)
except TypeError as err:
    print("sys-getunicodeinternedsize-diagnostic two-keyword", "at most 1 keyword argument" in str(err), "2 given" in str(err))
try:
    sys._is_interned(42)
except TypeError as err:
    print("sys-is-interned-type", "argument must be str" in str(err), "int" in str(err))
try:
    sys._is_interned()
except TypeError as err:
    print("sys-is-interned-arity", "exactly one argument" in str(err) or "1 argument" in str(err), "0 given" in str(err))
try:
    sys._is_interned(string=sys_intern_canonical)
except TypeError as err:
    print("sys-is-interned-keyword", "takes no keyword arguments" in str(err))
for sys_intern_bad_value in (b"abc", 42):
    try:
        sys.intern(sys_intern_bad_value)
    except TypeError as err:
        print("sys-intern-type", "argument must be str" in str(err), type(sys_intern_bad_value).__name__ in str(err))
try:
    sys.intern()
except TypeError as err:
    print("sys-intern-arity", "sys.intern()" in str(err), "exactly one argument" in str(err), "0 given" in str(err))
try:
    sys.intern("xlang3", "extra")
except TypeError as err:
    print("sys-intern-arity", "sys.intern()" in str(err), "exactly one argument" in str(err), "2 given" in str(err))
try:
    sys.intern(string="xlang3")
except TypeError as err:
    print("sys-intern-keyword", "takes no keyword arguments" in str(err))
print(sys._is_immortal(None), sys._is_immortal(True), sys._is_immortal(42), sys._is_immortal("abc"), sys._is_immortal([]))
try:
    sys._is_immortal()
except TypeError as err:
    print("sys-is-immortal-arity", "exactly one argument" in str(err) or "1 argument" in str(err), "0 given" in str(err))
try:
    sys._is_immortal(1, 2)
except TypeError as err:
    print("sys-is-immortal-arity", "2 given" in str(err))
try:
    sys._is_immortal(object=1)
except TypeError as err:
    print("sys-is-immortal-keyword", "takes no keyword arguments" in str(err))
sys_allocated_before = sys.getallocatedblocks()
sys_ref_target = []
print(sys.getrefcount(sys_ref_target) >= 2, sys.getrefcount(42) >= 1, sys.getallocatedblocks() >= sys_allocated_before)
try:
    sys.getallocatedblocks(x=1)
except TypeError as err:
    print("sys-getallocatedblocks-keyword", "takes no keyword arguments" in str(err))
try:
    sys.getrefcount()
except TypeError as err:
    print("sys-getrefcount-arity", "exactly one argument" in str(err) or "1 argument" in str(err), "0 given" in str(err))
try:
    sys.getrefcount(1, 2)
except TypeError as err:
    print("sys-getrefcount-arity", "2 given" in str(err))
try:
    sys.getrefcount(object=sys_ref_target)
except TypeError as err:
    print("sys-getrefcount-keyword", "takes no keyword arguments" in str(err))
print("sys-helper-docs", "previously interned string object" in sys.intern.__doc__, "Return the size of object in bytes." in sys.getsizeof.__doc__, "temporary) reference" in sys.getrefcount.__doc__, "specialized purposes only" in sys._is_immortal.__doc__)
sys_interner_size_text_signatures = {
    "intern": "($module, string, /)",
    "_is_interned": "($module, string, /)",
    "_is_immortal": "($module, op, /)",
    "getrefcount": "($module, object, /)",
    "getallocatedblocks": "($module, /)",
}
print("sys-interner-size-text-signatures", all(getattr(sys, name).__text_signature__ == signature for name, signature in sys_interner_size_text_signatures.items()), sys.getsizeof.__text_signature__ is None)
print(sys.stdin.readable(), sys.stdin.writable(), sys.stdout.writable(), sys.stderr.fileno(), sys.stdout.isatty(), sys.stderr.seekable(), sys.stdout.line_buffering, sys.stdout.closed)
stdio_method_names = ("read", "readline", "write", "flush", "close", "isatty", "readable", "writable", "seekable", "fileno")
stdio_streams = (sys.stdin, sys.stdout, sys.stderr)
print(
    all(getattr(stream, name).__name__ == name for stream in stdio_streams for name in stdio_method_names),
    all(getattr(stream, name).__qualname__ == "TextIOWrapper." + name for stream in stdio_streams for name in stdio_method_names),
    all(getattr(getattr(stream, name), "__module__", None) is None and getattr(getattr(stream, name), "__doc__", None) is None for stream in stdio_streams for name in stdio_method_names),
)
print(
    type(sys.stdin) is type(sys.stdout) is type(sys.stderr),
    type(sys.stdin).__name__,
    type(sys.stdin).__module__,
    type(sys.stdin).__qualname__,
    repr(type(sys.stdin)),
)
print(
    (sys.stdin.name, sys.stdin.mode, sys.stdin.line_buffering, sys.stdin.write_through, sys.stdin.newlines),
    (sys.stdout.name, sys.stdout.mode, sys.stdout.line_buffering, sys.stdout.write_through, sys.stdout.newlines),
    (sys.stderr.name, sys.stderr.mode, sys.stderr.line_buffering, sys.stderr.write_through, sys.stderr.newlines),
)
sys_stdio_keyword_count = 0
for sys_stdio_stream in stdio_streams:
    for sys_stdio_method_name in stdio_method_names:
        try:
            getattr(sys_stdio_stream, sys_stdio_method_name)(x=1)
        except TypeError as err:
            sys_stdio_keyword_message = str(err)
            if (
                "TextIOWrapper." + sys_stdio_method_name in sys_stdio_keyword_message
                and "takes no keyword arguments" in sys_stdio_keyword_message
            ):
                sys_stdio_keyword_count += 1
print("sys-stdio-keyword-typeerrors", sys_stdio_keyword_count, len(stdio_streams) * len(stdio_method_names))
sys_stdio_positional_cases = [
    ("read-many", lambda: sys.stdin.read(1, 2), ("read expected at most 1 argument", "got 2")),
    ("read-type", lambda: sys.stdin.read("x"), ("argument should be integer or None", "str")),
    ("readline-many", lambda: sys.stdin.readline(1, 2), ("readline expected at most 1 argument", "got 2")),
    ("readline-type", lambda: sys.stdin.readline("x"), ("str", "cannot be interpreted as an integer")),
    ("write-zero", lambda: sys.stdout.write(), ("TextIOWrapper.write", "exactly one argument", "0 given")),
    ("write-many", lambda: sys.stdout.write("a", "b"), ("TextIOWrapper.write", "exactly one argument", "2 given")),
    ("write-type", lambda: sys.stdout.write(1), ("write() argument must be str", "not int")),
]
for sys_stdio_bad_name, sys_stdio_bad_call, sys_stdio_bad_parts in sys_stdio_positional_cases:
    try:
        sys_stdio_bad_call()
    except TypeError as err:
        sys_stdio_bad_message = str(err)
        print("sys-stdio-positional-diagnostic", sys_stdio_bad_name, all(part in sys_stdio_bad_message for part in sys_stdio_bad_parts))
for sys_stdio_bad_name, sys_stdio_bad_call, sys_stdio_bad_parts in [
    ("stdin-readable", lambda: sys.stdin.readable(1), ("TextIOWrapper.readable", "takes no arguments", "1 given")),
    ("stdin-writable", lambda: sys.stdin.writable(1), ("TextIOWrapper.writable", "takes no arguments", "1 given")),
    ("stdout-isatty", lambda: sys.stdout.isatty(1), ("TextIOWrapper.isatty", "takes no arguments", "1 given")),
    ("stderr-seekable", lambda: sys.stderr.seekable(1), ("TextIOWrapper.seekable", "takes no arguments", "1 given")),
]:
    try:
        sys_stdio_bad_call()
    except TypeError as err:
        sys_stdio_bad_message = str(err)
        print("sys-stdio-capability-arity", sys_stdio_bad_name, all(part in sys_stdio_bad_message for part in sys_stdio_bad_parts))
# sys metadata structseq and startup attributes.
print(sys.version_info.major, sys.version_info[1], sys.implementation.version.micro, sys.implementation.cache_tag)
print("sys-version-shape", sys.version.startswith("3.14.7 (tags/v3.14.7:823f032, "), "[MSC v." in sys.version, "64 bit (AMD64)" in sys.version, "XLang3" not in sys.version)
print(sys._git[0], sys._git[1].startswith("tags/v3.14."), len(sys._git), isinstance(sys._vpath, str), sys._home is None, sys.float_repr_style)
print("sys-git-metadata", sys._git == ("CPython", "tags/v3.14.7", "823f032"))
print(isinstance(sys._stdlib_dir, str), sys._framework == "", (sys.platform == "win32") == hasattr(sys, "winver"), (sys.platform == "win32") == hasattr(sys, "dllhandle"), hasattr(sys, "abiflags") == (sys.platform != "win32"))
implementation_repr = repr(sys.implementation)
print(implementation_repr.startswith("namespace(name='xlang3'"), "cache_tag='xlang3-314'" in implementation_repr, "version=sys.version_info(" in implementation_repr, "supports_isolated_interpreters=False" in implementation_repr)
print(type(sys.implementation).__name__, type(sys.implementation).__module__, type(sys.implementation).__qualname__, type(sys.implementation).__doc__ == "A simple attribute-based namespace.", repr(sys.implementation).startswith("namespace("))
print(sys.implementation.supports_isolated_interpreters, sys.is_stack_trampoline_active(), sys._jit.is_enabled(), sys._jit.is_active(), sys._jit.is_available())
print((sys.platform == "win32" and not hasattr(sys.implementation, "_multiarch")) or (sys.platform != "win32" and hasattr(sys.implementation, "_multiarch")))
print(sys._jit.__doc__ == "Utilities for observing just-in-time compilation.", sys._jit.__package__ is None, sys._jit.__loader__ is None, sys._jit.__spec__ is None)
print(
    sys._jit.is_available.__name__, sys._jit.is_available.__qualname__, sys._jit.is_available.__module__, sys._jit.is_available.__doc__ is not None,
    sys._jit.is_enabled.__name__, sys._jit.is_enabled.__qualname__, sys._jit.is_enabled.__module__, sys._jit.is_enabled.__doc__ is not None,
    sys._jit.is_active.__name__, sys._jit.is_active.__qualname__, sys._jit.is_active.__module__, sys._jit.is_active.__doc__ is not None,
)
print("sys-jit-text-signatures", sys._jit.is_available.__text_signature__ == "($module, /)", sys._jit.is_enabled.__text_signature__ == "($module, /)", sys._jit.is_active.__text_signature__ == "($module, /)")
print(sys.monitoring.__doc__ is None, sys.monitoring.__package__ is None, sys.monitoring.__loader__ is None, sys.monitoring.__spec__ is None)
monitoring_events = sys.monitoring.events
monitoring_tool_id = 3
monitoring_code = (lambda: None).__code__
print(sys.monitoring.DEBUGGER_ID, sys.monitoring.COVERAGE_ID, sys.monitoring.PROFILER_ID, sys.monitoring.OPTIMIZER_ID, monitoring_events.NO_EVENTS, monitoring_events.LINE, monitoring_events.CALL, monitoring_events.BRANCH)
print(type(monitoring_events).__name__, type(monitoring_events).__module__, repr(monitoring_events).startswith("namespace(PY_START=1"), "C_RAISE=131072" in repr(monitoring_events), repr(monitoring_events).endswith("NO_EVENTS=0)"))
monitoring_function_names = ("use_tool_id", "free_tool_id", "clear_tool_id", "get_tool", "set_events", "get_events", "set_local_events", "get_local_events", "register_callback", "restart_events", "_all_events")
print(
    all(getattr(sys.monitoring, name).__name__ == name and getattr(sys.monitoring, name).__qualname__ == name for name in monitoring_function_names),
    all(getattr(sys.monitoring, name).__module__ == "sys.monitoring" for name in monitoring_function_names),
    all(getattr(sys.monitoring, name).__doc__ is None for name in monitoring_function_names),
)
monitoring_text_signatures = {
    "use_tool_id": "($module, tool_id, name, /)",
    "free_tool_id": "($module, tool_id, /)",
    "clear_tool_id": "($module, tool_id, /)",
    "get_tool": "($module, tool_id, /)",
    "set_events": "($module, tool_id, event_set, /)",
    "get_events": "($module, tool_id, /)",
    "set_local_events": "($module, tool_id, code, event_set, /)",
    "get_local_events": "($module, tool_id, code, /)",
    "register_callback": "($module, tool_id, event, func, /)",
    "restart_events": "($module, /)",
    "_all_events": "($module, /)",
}
print("monitoring-text-signatures", all(getattr(sys.monitoring, name).__text_signature__ == signature for name, signature in monitoring_text_signatures.items()))
print(sys.monitoring.get_tool(monitoring_tool_id) is None, sys.monitoring.use_tool_id(monitoring_tool_id, "fixture-monitor") is None, sys.monitoring.get_tool(monitoring_tool_id))
print(sys.monitoring.get_events(monitoring_tool_id), sys.monitoring.set_events(monitoring_tool_id, monitoring_events.LINE | monitoring_events.CALL) is None, sys.monitoring.get_events(monitoring_tool_id))
print(sys.monitoring.get_tool(True) is None, sys.monitoring.use_tool_id(True, "fixture-monitor-bool") is None, sys.monitoring.get_tool(1))
print(sys.monitoring.set_events(True, True) is None, sys.monitoring.get_events(1), sys.monitoring.free_tool_id(True) is None, sys.monitoring.get_tool(1) is None)
try:
    sys.monitoring.set_events(monitoring_tool_id, monitoring_events.C_RETURN)
except ValueError as err:
    print("monitoring-c-event", "independently" in str(err))
try:
    sys.monitoring.set_events(monitoring_tool_id, monitoring_events.C_RETURN | monitoring_events.C_RAISE)
except ValueError as err:
    print("monitoring-c-event-pair", "independently" in str(err))
print(sys.monitoring.get_local_events(monitoring_tool_id, monitoring_code), sys.monitoring.set_local_events(monitoring_tool_id, monitoring_code, monitoring_events.LINE) is None, sys.monitoring.get_local_events(monitoring_tool_id, monitoring_code))
try:
    sys.monitoring.set_local_events(monitoring_tool_id, monitoring_code, monitoring_events.C_RETURN | monitoring_events.C_RAISE)
except ValueError as err:
    print("monitoring-local-c-event-pair", "independently" in str(err))
def sys_monitoring_callback_probe(*args):
    return None
print(sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.LINE, sys_monitoring_callback_probe) is None, sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.LINE, None) is sys_monitoring_callback_probe)
print(sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.C_RETURN, sys_monitoring_callback_probe) is None, sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.C_RETURN, None) is sys_monitoring_callback_probe)
print(sys.monitoring.restart_events() is None, isinstance(sys.monitoring._all_events(), dict), sys.monitoring.free_tool_id(monitoring_tool_id) is None, sys.monitoring.get_tool(monitoring_tool_id) is None, sys.monitoring.get_events(monitoring_tool_id), sys.monitoring.get_local_events(monitoring_tool_id, monitoring_code))
print(sys.monitoring.use_tool_id(monitoring_tool_id, "fixture-monitor-clear") is None, sys.monitoring.set_events(monitoring_tool_id, monitoring_events.LINE) is None, sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.LINE, sys_monitoring_callback_probe) is None)
print(sys.monitoring.clear_tool_id(monitoring_tool_id) is None, sys.monitoring.get_tool(monitoring_tool_id), sys.monitoring.get_events(monitoring_tool_id), sys.monitoring.get_local_events(monitoring_tool_id, monitoring_code), sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.LINE, None) is None)
sys.monitoring.free_tool_id(monitoring_tool_id)
try:
    sys.monitoring.get_tool(99)
except ValueError as err:
    print("monitoring-tool-id", "between 0 and 5" in str(err))
try:
    sys.monitoring.use_tool_id(monitoring_tool_id, 42)
except ValueError as err:
    print("monitoring-tool-name", "str" in str(err))
try:
    sys.monitoring.set_events(monitoring_tool_id, monitoring_events.LINE)
except ValueError as err:
    print("monitoring-unused-tool", "not in use" in str(err))
try:
    sys.monitoring.set_local_events(monitoring_tool_id, monitoring_code, monitoring_events.LINE)
except ValueError as err:
    print("monitoring-unused-local-tool", "not in use" in str(err))
try:
    sys.monitoring.set_events(monitoring_tool_id, -1)
except ValueError as err:
    print("monitoring-event-set", "invalid event set" in str(err))
try:
    sys.monitoring.set_local_events(monitoring_tool_id, 42, monitoring_events.LINE)
except TypeError as err:
    print("monitoring-code-type", "code" in str(err))
try:
    sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.LINE | monitoring_events.CALL, None)
except ValueError as err:
    print("monitoring-callback-event", "one event" in str(err))
for monitoring_arity_name, monitoring_arity_call, monitoring_arity_parts in [
    ("use0", lambda: sys.monitoring.use_tool_id(), ("expected 2 arguments", "got 0")),
    ("use3", lambda: sys.monitoring.use_tool_id(monitoring_tool_id, "name", "extra"), ("expected 2 arguments", "got 3")),
    ("free0", lambda: sys.monitoring.free_tool_id(), ("takes exactly one argument", "0 given")),
    ("free2", lambda: sys.monitoring.free_tool_id(monitoring_tool_id, 1), ("takes exactly one argument", "2 given")),
    ("get_tool0", lambda: sys.monitoring.get_tool(), ("takes exactly one argument", "0 given")),
    ("clear2", lambda: sys.monitoring.clear_tool_id(monitoring_tool_id, 1), ("takes exactly one argument", "2 given")),
    ("set_events1", lambda: sys.monitoring.set_events(monitoring_tool_id), ("expected 2 arguments", "got 1")),
    ("get_events0", lambda: sys.monitoring.get_events(), ("takes exactly one argument", "0 given")),
    ("set_local2", lambda: sys.monitoring.set_local_events(monitoring_tool_id, monitoring_code), ("expected 3 arguments", "got 2")),
    ("get_local1", lambda: sys.monitoring.get_local_events(monitoring_tool_id), ("expected 2 arguments", "got 1")),
    ("callback2", lambda: sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.LINE), ("expected 3 arguments", "got 2")),
]:
    try:
        monitoring_arity_call()
    except TypeError as err:
        monitoring_arity_message = str(err)
        print("monitoring-arity", monitoring_arity_name, all(part in monitoring_arity_message for part in monitoring_arity_parts))
for monitoring_keyword_name, monitoring_keyword_call in [
    ("use_tool_id", lambda: sys.monitoring.use_tool_id(tool_id=monitoring_tool_id, name="name")),
    ("free_tool_id", lambda: sys.monitoring.free_tool_id(tool_id=monitoring_tool_id)),
    ("clear_tool_id", lambda: sys.monitoring.clear_tool_id(tool_id=monitoring_tool_id)),
    ("get_tool", lambda: sys.monitoring.get_tool(tool_id=monitoring_tool_id)),
    ("set_events", lambda: sys.monitoring.set_events(tool_id=monitoring_tool_id, event_set=0)),
    ("get_events", lambda: sys.monitoring.get_events(tool_id=monitoring_tool_id)),
    ("set_local_events", lambda: sys.monitoring.set_local_events(tool_id=monitoring_tool_id, code=monitoring_code, event_set=0)),
    ("get_local_events", lambda: sys.monitoring.get_local_events(tool_id=monitoring_tool_id, code=monitoring_code)),
    ("register_callback", lambda: sys.monitoring.register_callback(tool_id=monitoring_tool_id, event=monitoring_events.LINE, func=None)),
    ("restart_events", lambda: sys.monitoring.restart_events(x=1)),
    ("_all_events", lambda: sys.monitoring._all_events(x=1)),
]:
    try:
        monitoring_keyword_call()
    except TypeError as err:
        print("monitoring-keyword", monitoring_keyword_name, "takes no keyword arguments" in str(err))
monitoring_live_events = []
def sys_monitoring_live_callback(code, instruction_offset, *args):
    monitoring_live_events.append((code.co_name, isinstance(instruction_offset, int), len(args), args[0] if args else None))

def sys_monitoring_live_target():
    return "monitoring-live"

print(sys.monitoring.use_tool_id(monitoring_tool_id, "fixture-monitor-live") is None)
print(sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.PY_START, sys_monitoring_live_callback) is None, sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.PY_RETURN, sys_monitoring_live_callback) is None)
print(sys.monitoring.set_events(monitoring_tool_id, monitoring_events.PY_START | monitoring_events.PY_RETURN) is None, sys_monitoring_live_target())
sys.monitoring.set_events(monitoring_tool_id, 0)
sys.monitoring.free_tool_id(monitoring_tool_id)
print(monitoring_live_events[0][0], monitoring_live_events[0][1], monitoring_live_events[0][2], monitoring_live_events[1][0], monitoring_live_events[1][1], monitoring_live_events[1][2], monitoring_live_events[1][3])
monitoring_generator_events = []
def sys_monitoring_generator_callback(code, instruction_offset, *args):
    monitoring_generator_events.append((code.co_name, isinstance(instruction_offset, int), len(args), args[0] if args else None))

def sys_monitoring_generator_target():
    yield "monitoring-yield"
    return "monitoring-return"

print(sys.monitoring.use_tool_id(monitoring_tool_id, "fixture-monitor-generator") is None)
print(sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.PY_RESUME, sys_monitoring_generator_callback) is None, sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.PY_YIELD, sys_monitoring_generator_callback) is None)
monitoring_generator = sys_monitoring_generator_target()
print(sys.monitoring.set_events(monitoring_tool_id, monitoring_events.PY_RESUME | monitoring_events.PY_YIELD) is None, next(monitoring_generator))
try:
    next(monitoring_generator)
except StopIteration as err:
    print("monitoring-generator-stop", err.value)
sys.monitoring.set_events(monitoring_tool_id, 0)
sys.monitoring.free_tool_id(monitoring_tool_id)
print(len(monitoring_generator_events), monitoring_generator_events[0], monitoring_generator_events[1])
monitoring_throw_events = []
def sys_monitoring_throw_callback(code, instruction_offset, exception):
    monitoring_throw_events.append((code.co_name, isinstance(instruction_offset, int), type(exception).__name__, str(exception)))

def sys_monitoring_throw_target():
    try:
        yield "throw-start"
    except ValueError as err:
        yield str(err)

print(sys.monitoring.use_tool_id(monitoring_tool_id, "fixture-monitor-throw") is None)
print(sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.PY_THROW, sys_monitoring_throw_callback) is None)
monitoring_throw_generator = sys_monitoring_throw_target()
print(next(monitoring_throw_generator), sys.monitoring.set_events(monitoring_tool_id, monitoring_events.PY_THROW) is None, monitoring_throw_generator.throw(ValueError("monitoring-throw")))
sys.monitoring.set_events(monitoring_tool_id, 0)
sys.monitoring.free_tool_id(monitoring_tool_id)
print(len(monitoring_throw_events), monitoring_throw_events[0])
monitoring_local_throw_events = []
def sys_monitoring_local_throw_callback(code, instruction_offset, exception):
    monitoring_local_throw_events.append((code.co_name, isinstance(instruction_offset, int), type(exception).__name__, str(exception)))

def sys_monitoring_local_throw_target():
    try:
        yield "local-throw-start"
    except ValueError as err:
        yield str(err)

def sys_monitoring_local_throw_other():
    try:
        yield "other-throw-start"
    except ValueError as err:
        yield str(err)

print(sys.monitoring.use_tool_id(monitoring_tool_id, "fixture-monitor-local-throw") is None)
print(sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.PY_THROW, sys_monitoring_local_throw_callback) is None)
monitoring_local_throw_generator = sys_monitoring_local_throw_target()
monitoring_local_throw_other_generator = sys_monitoring_local_throw_other()
print(next(monitoring_local_throw_generator), next(monitoring_local_throw_other_generator), sys.monitoring.set_local_events(monitoring_tool_id, sys_monitoring_local_throw_target.__code__, monitoring_events.PY_THROW) is None, monitoring_local_throw_generator.throw(ValueError("local-monitoring-throw")), monitoring_local_throw_other_generator.throw(ValueError("other-monitoring-throw")))
sys.monitoring.set_local_events(monitoring_tool_id, sys_monitoring_local_throw_target.__code__, 0)
sys.monitoring.free_tool_id(monitoring_tool_id)
print(len(monitoring_local_throw_events), monitoring_local_throw_events[0])
monitoring_local_events = []
def sys_monitoring_local_callback(code, instruction_offset, *args):
    monitoring_local_events.append((code.co_name, isinstance(instruction_offset, int), len(args), args[0] if args else None))

def sys_monitoring_local_target():
    return "monitoring-local"

def sys_monitoring_local_other():
    return "monitoring-other"

print(sys.monitoring.use_tool_id(monitoring_tool_id, "fixture-monitor-local") is None)
print(sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.PY_START, sys_monitoring_local_callback) is None, sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.PY_RETURN, sys_monitoring_local_callback) is None)
print(sys.monitoring.set_local_events(monitoring_tool_id, sys_monitoring_local_target.__code__, monitoring_events.PY_START | monitoring_events.PY_RETURN) is None, sys_monitoring_local_target(), sys_monitoring_local_other())
sys.monitoring.set_local_events(monitoring_tool_id, sys_monitoring_local_target.__code__, 0)
sys.monitoring.free_tool_id(monitoring_tool_id)
print(len(monitoring_local_events), monitoring_local_events[0][0], monitoring_local_events[0][1], monitoring_local_events[0][2], monitoring_local_events[1][0], monitoring_local_events[1][1], monitoring_local_events[1][2], monitoring_local_events[1][3])
monitoring_line_events = []
def sys_monitoring_line_callback(code, instruction_offset):
    monitoring_line_events.append((code.co_name, isinstance(instruction_offset, int)))

def sys_monitoring_line_target():
    marker = "line"
    return marker

print(sys.monitoring.use_tool_id(monitoring_tool_id, "fixture-monitor-line") is None)
print(sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.LINE, sys_monitoring_line_callback) is None)
print(sys.monitoring.set_events(monitoring_tool_id, monitoring_events.LINE) is None, sys_monitoring_line_target())
sys.monitoring.set_events(monitoring_tool_id, 0)
sys.monitoring.free_tool_id(monitoring_tool_id)
print(len(monitoring_line_events) >= 2, "sys_monitoring_line_target" in [event[0] for event in monitoring_line_events], all(event[1] for event in monitoring_line_events))
monitoring_local_line_events = []
def sys_monitoring_local_line_callback(code, instruction_offset):
    monitoring_local_line_events.append(code.co_name)

def sys_monitoring_local_line_target():
    value = "local-line"
    return value

def sys_monitoring_local_line_other():
    value = "other-line"
    return value

print(sys.monitoring.use_tool_id(monitoring_tool_id, "fixture-monitor-local-line") is None)
print(sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.LINE, sys_monitoring_local_line_callback) is None)
print(sys.monitoring.set_local_events(monitoring_tool_id, sys_monitoring_local_line_target.__code__, monitoring_events.LINE) is None, sys_monitoring_local_line_target(), sys_monitoring_local_line_other())
sys.monitoring.set_local_events(monitoring_tool_id, sys_monitoring_local_line_target.__code__, 0)
sys.monitoring.free_tool_id(monitoring_tool_id)
print(len(monitoring_local_line_events) >= 1, "sys_monitoring_local_line_target" in monitoring_local_line_events, "sys_monitoring_local_line_other" in monitoring_local_line_events)
monitoring_instruction_events = []
def sys_monitoring_instruction_callback(code, instruction_offset):
    monitoring_instruction_events.append((code.co_name, isinstance(instruction_offset, int), instruction_offset >= 0))

def sys_monitoring_instruction_target():
    left = 2
    right = 3
    return left + right

print(sys.monitoring.use_tool_id(monitoring_tool_id, "fixture-monitor-instruction") is None)
print(sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.INSTRUCTION, sys_monitoring_instruction_callback) is None)
print(sys.monitoring.set_events(monitoring_tool_id, monitoring_events.INSTRUCTION) is None, sys_monitoring_instruction_target())
sys.monitoring.set_events(monitoring_tool_id, 0)
sys.monitoring.free_tool_id(monitoring_tool_id)
print(len(monitoring_instruction_events) >= 3, "sys_monitoring_instruction_target" in [event[0] for event in monitoring_instruction_events], all(event[1] and event[2] for event in monitoring_instruction_events))
monitoring_local_instruction_events = []
def sys_monitoring_local_instruction_callback(code, instruction_offset):
    monitoring_local_instruction_events.append(code.co_name)

def sys_monitoring_local_instruction_target():
    value = 7
    return value

def sys_monitoring_local_instruction_other():
    value = 8
    return value

print(sys.monitoring.use_tool_id(monitoring_tool_id, "fixture-monitor-local-instruction") is None)
print(sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.INSTRUCTION, sys_monitoring_local_instruction_callback) is None)
print(sys.monitoring.set_local_events(monitoring_tool_id, sys_monitoring_local_instruction_target.__code__, monitoring_events.INSTRUCTION) is None, sys_monitoring_local_instruction_target(), sys_monitoring_local_instruction_other())
sys.monitoring.set_local_events(monitoring_tool_id, sys_monitoring_local_instruction_target.__code__, 0)
sys.monitoring.free_tool_id(monitoring_tool_id)
print(len(monitoring_local_instruction_events) >= 2, "sys_monitoring_local_instruction_target" in monitoring_local_instruction_events, "sys_monitoring_local_instruction_other" in monitoring_local_instruction_events)
monitoring_branch_events = []
def sys_monitoring_branch_callback(code, instruction_offset, destination_offset):
    monitoring_branch_events.append((code.co_name, isinstance(instruction_offset, int), isinstance(destination_offset, int), destination_offset != instruction_offset))

def sys_monitoring_branch_target(flag):
    if flag:
        return "left"
    return "right"

def sys_monitoring_jump_target():
    total = 0
    while total < 1:
        total += 1
    return total

print(sys.monitoring.use_tool_id(monitoring_tool_id, "fixture-monitor-branch") is None)
print(sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.BRANCH_LEFT, sys_monitoring_branch_callback) is None, sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.BRANCH_RIGHT, sys_monitoring_branch_callback) is None, sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.JUMP, sys_monitoring_branch_callback) is None)
print(sys.monitoring.set_events(monitoring_tool_id, monitoring_events.BRANCH_LEFT | monitoring_events.BRANCH_RIGHT | monitoring_events.JUMP) is None, sys_monitoring_branch_target(True), sys_monitoring_branch_target(False), sys_monitoring_jump_target())
sys.monitoring.set_events(monitoring_tool_id, 0)
sys.monitoring.free_tool_id(monitoring_tool_id)
print(any(event[0] == "sys_monitoring_branch_target" and event[1] and event[2] and event[3] for event in monitoring_branch_events), any(event[0] == "sys_monitoring_jump_target" and event[1] and event[2] and event[3] for event in monitoring_branch_events), len(monitoring_branch_events) >= 4)
monitoring_local_branch_events = []
def sys_monitoring_local_branch_callback(code, instruction_offset, destination_offset):
    monitoring_local_branch_events.append(code.co_name)

def sys_monitoring_local_branch_target(flag):
    if flag:
        return "local-left"
    return "local-right"

def sys_monitoring_local_branch_other(flag):
    if flag:
        return "other-left"
    return "other-right"

print(sys.monitoring.use_tool_id(monitoring_tool_id, "fixture-monitor-local-branch") is None)
print(sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.BRANCH_LEFT, sys_monitoring_local_branch_callback) is None, sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.BRANCH_RIGHT, sys_monitoring_local_branch_callback) is None)
print(sys.monitoring.set_local_events(monitoring_tool_id, sys_monitoring_local_branch_target.__code__, monitoring_events.BRANCH_LEFT | monitoring_events.BRANCH_RIGHT) is None, sys_monitoring_local_branch_target(True), sys_monitoring_local_branch_target(False), sys_monitoring_local_branch_other(True))
sys.monitoring.set_local_events(monitoring_tool_id, sys_monitoring_local_branch_target.__code__, 0)
sys.monitoring.free_tool_id(monitoring_tool_id)
print(monitoring_local_branch_events.count("sys_monitoring_local_branch_target") >= 2, "sys_monitoring_local_branch_other" in monitoring_local_branch_events)
monitoring_c_events = []
def sys_monitoring_c_callback(code, instruction_offset, callable):
    monitoring_c_events.append((getattr(code, "co_name", None), isinstance(instruction_offset, int), callable))

print(sys.monitoring.use_tool_id(monitoring_tool_id, "fixture-monitor-c") is None)
print(sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.CALL, sys_monitoring_c_callback) is None, sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.C_RETURN, sys_monitoring_c_callback) is None, sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.C_RAISE, sys_monitoring_c_callback) is None)
print(sys.monitoring.set_events(monitoring_tool_id, monitoring_events.CALL) is None, sys.monitoring.get_tool(monitoring_tool_id))
try:
    sys.monitoring.get_tool(99)
except ValueError:
    pass
sys.monitoring.set_events(monitoring_tool_id, 0)
sys.monitoring.free_tool_id(monitoring_tool_id)
print(any(event[1] and event[2] == "sys.monitoring.get_tool" for event in monitoring_c_events), len([event for event in monitoring_c_events if event[2] == "sys.monitoring.get_tool"]) >= 4)
monitoring_exception_events = []
def sys_monitoring_exception_callback(code, instruction_offset, exception):
    monitoring_exception_events.append((code.co_name, isinstance(instruction_offset, int), type(exception).__name__, str(exception)))

def sys_monitoring_exception_target():
    try:
        raise ValueError("monitoring-boom")
    except ValueError:
        return "monitoring-caught"

print(sys.monitoring.use_tool_id(monitoring_tool_id, "fixture-monitor-exception") is None)
print(sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.RAISE, sys_monitoring_exception_callback) is None, sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.EXCEPTION_HANDLED, sys_monitoring_exception_callback) is None)
print(sys.monitoring.set_events(monitoring_tool_id, monitoring_events.RAISE | monitoring_events.EXCEPTION_HANDLED) is None, sys_monitoring_exception_target())
sys.monitoring.set_events(monitoring_tool_id, 0)
sys.monitoring.free_tool_id(monitoring_tool_id)
print(len(monitoring_exception_events), monitoring_exception_events[0], monitoring_exception_events[1])
monitoring_reraise_events = []
def sys_monitoring_reraise_callback(code, instruction_offset, exception):
    monitoring_reraise_events.append((code.co_name, isinstance(instruction_offset, int), type(exception).__name__, str(exception)))

def sys_monitoring_reraise_target():
    try:
        raise ValueError("monitoring-reraise")
    except ValueError:
        raise

print(sys.monitoring.use_tool_id(monitoring_tool_id, "fixture-monitor-reraise") is None)
print(sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.RERAISE, sys_monitoring_reraise_callback) is None)
sys.monitoring.set_events(monitoring_tool_id, monitoring_events.RERAISE)
try:
    sys_monitoring_reraise_target()
except ValueError:
    pass
sys.monitoring.set_events(monitoring_tool_id, 0)
sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.RERAISE, None)
sys.monitoring.free_tool_id(monitoring_tool_id)
print(len(monitoring_reraise_events) >= 1, monitoring_reraise_events[0])
monitoring_unwind_events = []
def sys_monitoring_unwind_callback(code, instruction_offset, exception):
    monitoring_unwind_events.append((code.co_name, isinstance(instruction_offset, int), type(exception).__name__, str(exception)))

def sys_monitoring_unwind_inner():
    raise ValueError("monitoring-unwind")

def sys_monitoring_unwind_outer():
    sys_monitoring_unwind_inner()

print(sys.monitoring.use_tool_id(monitoring_tool_id, "fixture-monitor-unwind") is None)
print(sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.PY_UNWIND, sys_monitoring_unwind_callback) is None)
sys.monitoring.set_events(monitoring_tool_id, monitoring_events.PY_UNWIND)
try:
    sys_monitoring_unwind_outer()
except ValueError:
    pass
sys.monitoring.set_events(monitoring_tool_id, 0)
sys.monitoring.register_callback(monitoring_tool_id, monitoring_events.PY_UNWIND, None)
sys.monitoring.free_tool_id(monitoring_tool_id)
print(len(monitoring_unwind_events), monitoring_unwind_events[0], monitoring_unwind_events[1])
print(sys.flags.optimize, sys.flags.utf8_mode, sys.flags.safe_path, len(sys.flags) > 10)
print(repr(sys.version_info), repr(sys.flags).startswith("sys.flags("), "gil=" not in repr(sys.flags), repr(sys.hash_info).startswith("sys.hash_info("))
print(sys.dont_write_bytecode, sys.flags.dont_write_bytecode, sys.flags.hash_randomization, sys.flags.utf8_mode)
print(sys.flags.n_sequence_fields, sys.flags.n_fields, sys.flags.gil, sys.flags.thread_inherit_context, sys.flags.context_aware_warnings, len(sys.flags))
print(type(sys.version_info).n_fields, type(sys.version_info).major.__name__, type(sys.flags).n_fields, type(sys.flags).gil.__name__)
print("sys-structseq-type-metadata", all((type(getattr(sys, name)).__module__, type(getattr(sys, name)).__qualname__) == ("sys", name) for name in ("version_info", "flags", "int_info", "float_info", "hash_info", "thread_info")), all(repr(type(getattr(sys, name))) == "<class 'sys." + name + "'>" and str(type(getattr(sys, name))) == "<class 'sys." + name + "'>" for name in ("version_info", "flags", "int_info", "float_info", "hash_info", "thread_info")))
print(type(sys.version_info).__match_args__, type(sys.flags).__match_args__[-1], len(type(sys.flags).__match_args__), "gil" not in type(sys.flags).__match_args__)
sys_version_major_descriptor = type(sys.version_info).major
sys_flags_debug_descriptor = type(sys.flags).debug
print(type(sys_version_major_descriptor).__name__, type(sys_version_major_descriptor).__module__, sys_version_major_descriptor.__objclass__ is type(sys.version_info), sys_version_major_descriptor.__get__(sys.version_info), inspect.ismemberdescriptor(sys_version_major_descriptor), repr(sys_version_major_descriptor) == "<member 'major' of 'sys.version_info' objects>", repr(sys_flags_debug_descriptor) == "<member 'debug' of 'sys.flags' objects>")
print(sys.float_info.radix, sys.float_info.mant_dig, sys.hash_info.width, sys.thread_info.name)
print(type(sys.float_info).n_fields, type(sys.hash_info).width.__name__, type(sys.thread_info).n_sequence_fields, type(sys.thread_info).name.__name__)
print(type(sys.float_info).__match_args__[0], type(sys.hash_info).__match_args__[-1], type(sys.thread_info).__match_args__)
print(isinstance(sys.version_info, tuple), sys.version_info.count(3), sys.version_info.index("final"), list(sys.version_info)[0])
for sys_structseq_method_bad_name, sys_structseq_method_bad_call, sys_structseq_method_bad_parts in [
    ("count-missing", lambda: sys.version_info.count(), ("tuple.count()", "exactly one argument", "0 given")),
    ("count-extra", lambda: sys.version_info.count(1, 2), ("tuple.count()", "exactly one argument", "2 given")),
    ("count-keyword", lambda: sys.version_info.count(value=3), ("tuple.count()", "takes no keyword arguments")),
    ("count-unbound-missing", lambda: type(sys.version_info).count(), ("unbound method tuple.count()", "needs an argument")),
    ("count-receiver", lambda: type(sys.version_info).count([], 1), ("descriptor 'count'", "tuple", "list")),
    ("index-missing", lambda: sys.version_info.index(), ("index expected at least 1 argument", "got 0")),
    ("index-extra", lambda: sys.version_info.index(1, 0, 1, 2), ("index expected at most 3 arguments", "got 4")),
    ("index-keyword", lambda: sys.version_info.index(value=3), ("tuple.index()", "takes no keyword arguments")),
    ("index-unbound-missing", lambda: type(sys.version_info).index(), ("unbound method tuple.index()", "needs an argument")),
    ("index-receiver", lambda: type(sys.version_info).index([], 1), ("descriptor 'index'", "tuple", "list")),
    ("repr-extra", lambda: sys.version_info.__repr__(1), ("expected 0 arguments", "got 1")),
    ("repr-keyword", lambda: sys.version_info.__repr__(x=1), ("wrapper __repr__()", "takes no keyword arguments")),
    ("repr-unbound-missing", lambda: type(sys.version_info).__repr__(), ("descriptor '__repr__'", "needs an argument")),
    ("repr-receiver", lambda: type(sys.version_info).__repr__([]), ("descriptor '__repr__'", "list")),
]:
    try:
        sys_structseq_method_bad_call()
    except TypeError as err:
        sys_structseq_method_bad_message = str(err)
        print("sys-structseq-method-diagnostic", sys_structseq_method_bad_name, all(part in sys_structseq_method_bad_message for part in sys_structseq_method_bad_parts))
print(isinstance(sys.flags, tuple), sys.flags.count(sys.flags.gil) >= 1, sys.flags.index(sys.flags.int_max_str_digits), sys.flags.n_fields > len(sys.flags))
print(sys.flags.index(0, True), sys.flags.index(sys.flags.int_max_str_digits, False), sys.flags[-1])
try:
    sys.flags[len(sys.flags)]
except IndexError as err:
    print("sys-flags-index-error", "range" in str(err) or "out of range" in str(err))
print(isinstance(sys.float_info, tuple), sys.float_info.count(sys.float_info.radix), sys.float_info.index(sys.float_info.radix), list(sys.float_info)[-1])
print("sys-float-info-repr-precision", "1.7976931348623157e+308" in repr(sys.float_info), "2.2250738585072014e-308" in repr(sys.float_info), "2.220446049250313e-16" in repr(sys.float_info), repr(1.0) == "1.0", str(1000000.0) == "1000000.0", repr(1e-6) == "1e-06")
print(isinstance(sys.hash_info, tuple), sys.hash_info.index(sys.hash_info.algorithm), sys.hash_info.count(sys.hash_info.cutoff) >= 1)
print(isinstance(sys.thread_info, tuple), sys.thread_info.index(sys.thread_info.name), sys.thread_info.count(sys.thread_info.name))
print(sys.maxunicode, sys.hexversion > 0, sys.executable.endswith(".exe"), sys.prefix != "")
print("sys" in sys.builtin_module_names, sys.pycache_prefix is None, isinstance(sys.orig_argv, list))
print("sys-builtin-module-names", len(sys.builtin_module_names), sys.builtin_module_names[:3], sys.builtin_module_names[-4:], all(name in sys.builtin_module_names for name in ("_bisect", "_contextvars", "_datetime", "_opcode", "_winapi", "array", "binascii", "errno", "gc", "marshal", "mmap", "msvcrt", "nt", "winreg", "xxsubtype")), not any(name in sys.builtin_module_names for name in ("_builtins", "abc", "json")))
print(sys.executable == sys._base_executable, sys.prefix == sys.base_prefix == sys.exec_prefix == sys.base_exec_prefix, not hasattr(sys, "real_prefix"), len(sys.orig_argv) >= 1, sys.orig_argv[0] == sys.executable)
print(isinstance(sys.warnoptions, list), isinstance(sys._xoptions, dict), isinstance(sys.dont_write_bytecode, bool), sys.api_version > 0, hasattr(sys, "abiflags") == (sys.platform != "win32"), sys.byteorder in ("little", "big"), sys.platlibdir in ("DLLs", "lib"))
print(isinstance(sys._stdlib_dir, str), sys._stdlib_dir.endswith("Lib"), sys._framework == "", sys.winver == "3.14")
windows_version = sys.getwindowsversion()
print(windows_version.major >= 0, len(windows_version), windows_version.n_fields, isinstance(windows_version.platform_version, tuple))
print("sys-windowsversion-type", type(windows_version).__name__, type(windows_version).__module__, type(windows_version).__qualname__, repr(type(windows_version)))
print(type(windows_version).n_fields, type(windows_version).platform_version.__name__)
print(repr(type(windows_version).major) == "<member 'major' of 'sys.getwindowsversion' objects>")
print(isinstance(windows_version, tuple), windows_version.index(windows_version.platform), windows_version.count(windows_version.service_pack) >= 1)
print(repr(windows_version).startswith("sys.getwindowsversion("), "platform_version" not in repr(windows_version))
print(sys._enablelegacywindowsfsencoding() is None, sys._debugmallocstats() is None, isinstance(sys.dllhandle, int))
print(sys.getfilesystemencoding(), sys.getfilesystemencodeerrors())
try:
    sys.activate_stack_trampoline("perf")
except ValueError as err:
    print("stack-trampoline", "not available" in str(err))
for sys_stack_bad_name, sys_stack_bad_call, sys_stack_bad_parts in [
    ("missing", lambda: sys.activate_stack_trampoline(), ("exactly one argument", "0 given")),
    ("extra", lambda: sys.activate_stack_trampoline("perf", "x"), ("exactly one argument", "2 given")),
    ("type", lambda: sys.activate_stack_trampoline(1), ("argument must be str", "int")),
]:
    try:
        sys_stack_bad_call()
    except TypeError as err:
        sys_stack_bad_message = str(err)
        print("sys-stack-trampoline-diagnostic", sys_stack_bad_name, all(part in sys_stack_bad_message for part in sys_stack_bad_parts))
print(sys.deactivate_stack_trampoline() is None, sys.is_stack_trampoline_active())
print("sys-stack-trampoline-docs", "no stack profiler is activated" in sys.deactivate_stack_trampoline.__doc__)
old_switch = sys.getswitchinterval()
sys.setswitchinterval(0.002)
old_recursion_limit = sys.getrecursionlimit()
old_int_max_digits = sys.get_int_max_str_digits()
print(sys.getswitchinterval() > 0, old_int_max_digits, sys.flags.int_max_str_digits, sys.int_info.default_max_str_digits, sys.int_info.str_digits_check_threshold)
sys.setswitchinterval(True)
print(sys.getswitchinterval() == 1.0)
try:
    sys.setswitchinterval(False)
except ValueError as err:
    print("switchinterval-bool", "strictly positive" in str(err))
for sys_switch_bad_name, sys_switch_bad_call, sys_switch_bad_parts in [
    ("missing", lambda: sys.setswitchinterval(), ("exactly one argument", "0 given")),
    ("extra", lambda: sys.setswitchinterval(0.1, 0.2), ("exactly one argument", "2 given")),
    ("type", lambda: sys.setswitchinterval("x"), ("real number", "str")),
]:
    try:
        sys_switch_bad_call()
    except TypeError as err:
        sys_switch_bad_message = str(err)
        print("sys-switchinterval-diagnostic", sys_switch_bad_name, all(part in sys_switch_bad_message for part in sys_switch_bad_parts))
try:
    sys.setrecursionlimit(True)
except RecursionError as err:
    print("recursionlimit-bool", "limit is too low" in str(err))
try:
    sys.setrecursionlimit(False)
except ValueError as err:
    print("recursionlimit-bool", "greater or equal" in str(err))
for sys_recursion_bad_name, sys_recursion_bad_call, sys_recursion_bad_parts in [
    ("missing", lambda: sys.setrecursionlimit(), ("exactly one argument", "0 given")),
    ("extra", lambda: sys.setrecursionlimit(1000, 2), ("exactly one argument", "2 given")),
    ("type", lambda: sys.setrecursionlimit("x"), ("str", "cannot be interpreted as an integer")),
]:
    try:
        sys_recursion_bad_call()
    except TypeError as err:
        sys_recursion_bad_message = str(err)
        print("sys-recursionlimit-diagnostic", sys_recursion_bad_name, all(part in sys_recursion_bad_message for part in sys_recursion_bad_parts))
sys.setrecursionlimit(old_recursion_limit)
print("sys-runtime-helper-docs", "maximum depth of the Python interpreter" in sys.getrecursionlimit.__doc__, "highest possible limit" in sys.setrecursionlimit.__doc__, "uninterruptible code" in sys.setswitchinterval.__doc__, "Platform_version is a 3-tuple" in sys.getwindowsversion.__doc__, "internal consistency" in sys._debugmallocstats.__doc__, "PYTHONLEGACYWINDOWSFSENCODING" in sys._enablelegacywindowsfsencoding.__doc__)
sys.set_int_max_str_digits(640)
print(sys.get_int_max_str_digits(), sys.flags.int_max_str_digits, sys.flags[17], sys.int_info[2], sys.int_info[3])
sys.set_int_max_str_digits(0)
print(sys.get_int_max_str_digits(), sys.flags.int_max_str_digits, sys.flags[17])
sys.set_int_max_str_digits(False)
print(sys.get_int_max_str_digits(), sys.flags.int_max_str_digits, sys.flags[17])
sys.set_int_max_str_digits(maxdigits=640)
print("sys-int-max-str-digits-keyword", sys.get_int_max_str_digits() == 640, sys.flags.int_max_str_digits == 640, sys.flags[17] == 640)
sys.set_int_max_str_digits(maxdigits=0)
print("sys-int-max-str-digits-keyword", sys.get_int_max_str_digits() == 0, sys.flags.int_max_str_digits == 0, sys.flags[17] == 0)
try:
    sys.set_int_max_str_digits(True)
except ValueError as err:
    print("int-max-str-digits-bool", ">= 640" in str(err), "0 for unlimited" in str(err))
for sys_int_digits_bad_name, sys_int_digits_bad_call, sys_int_digits_bad_parts in [
    ("missing", lambda: sys.set_int_max_str_digits(), ("missing required argument", "maxdigits")),
    ("extra", lambda: sys.set_int_max_str_digits(1, 2), ("at most 1 argument", "2 given")),
    ("type", lambda: sys.set_int_max_str_digits("x"), ("str", "cannot be interpreted as an integer")),
    ("keyword-missing", lambda: sys.set_int_max_str_digits(value=640), ("missing required argument", "maxdigits")),
    ("keyword-duplicate", lambda: sys.set_int_max_str_digits(640, maxdigits=640), ("at most 1 argument", "2 given")),
    ("keyword-extra", lambda: sys.set_int_max_str_digits(maxdigits=640, value=0), ("at most 1 keyword argument", "2 given")),
]:
    try:
        sys_int_digits_bad_call()
    except TypeError as err:
        sys_int_digits_bad_message = str(err)
        print("sys-int-max-str-digits-diagnostic", sys_int_digits_bad_name, all(part in sys_int_digits_bad_message for part in sys_int_digits_bad_parts))
sys.set_int_max_str_digits(old_int_max_digits)
sys.setswitchinterval(old_switch)
try:
    sys.set_int_max_str_digits(1)
except ValueError as err:
    print("int-max-str-digits", ">= 640" in str(err), "0 for unlimited" in str(err))
def sys_profile_probe(frame, event, arg):
    return None
sys.setprofile(sys_profile_probe)
print(sys.getprofile() is sys_profile_probe, sys.is_finalizing())
sys.setprofile(None)
sys._setprofileallthreads(sys_profile_probe)
print(sys.getprofile() is sys_profile_probe, sys._setprofileallthreads(None) is None, sys.getprofile() is None)
sys_profile_events = []
def sys_profile_callback_helper():
    sys_profile_events.append(("callback-helper", None, None))
def sys_profile_dispatch_probe(frame, event, arg):
    name = frame.f_code.co_name
    if name in ("sys_profile_target", "sys_profile_callback_helper"):
        if event == "exception":
            sys_profile_events.append((name, event, arg[0].__name__))
        else:
            sys_profile_events.append((name, event, arg))
    sys_profile_callback_helper()
def sys_profile_target(should_return):
    if should_return:
        return "profile-return"
    raise ValueError("profile-exception")
sys.setprofile(sys_profile_dispatch_probe)
print(sys_profile_target(True))
try:
    sys_profile_target(False)
except ValueError:
    pass
sys.setprofile(None)
print(
    ("sys_profile_target", "call", None) in sys_profile_events,
    ("sys_profile_target", "return", "profile-return") in sys_profile_events,
    ("sys_profile_target", "exception", "ValueError") in sys_profile_events,
    not any(item[0] == "sys_profile_callback_helper" for item in sys_profile_events),
)
sys_profile_c_events = []
def sys_profile_c_probe(frame, event, arg):
    if frame.f_code.co_name == "sys_profile_c_target" and event.startswith("c_"):
        sys_profile_c_events.append((event, str(arg)))
def sys_profile_c_target():
    eval("1 + 2")
    try:
        eval("chr(-1)")
    except ValueError:
        pass
sys.setprofile(sys_profile_c_probe)
sys_profile_c_target()
sys.setprofile(None)
print(
    any(event == "c_call" for event, _ in sys_profile_c_events),
    any(event == "c_return" for event, _ in sys_profile_c_events),
    any(event == "c_exception" for event, _ in sys_profile_c_events),
)
threading_profile_events = []
threading_profile_ident = []
def threading_profile_probe(frame, event, arg):
    if frame.f_code.co_name == "threading_profile_worker" and event == "call":
        threading_profile_events.append((threading.get_ident(), arg))
def threading_profile_worker():
    threading_profile_ident.append(threading.get_ident())
threading.setprofile(threading_profile_probe)
print(threading.getprofile() is threading_profile_probe)
threading_profile_thread = threading.Thread(target=threading_profile_worker)
threading_profile_thread.start()
threading_profile_thread.join()
threading.setprofile(None)
print(len(threading_profile_events) >= 1, threading_profile_events[0][0] == threading_profile_ident[0], threading.getprofile() is None)
def sys_trace_probe(frame, event, arg):
    return sys_trace_probe
def sys_call_tracing_probe(left, right):
    return left + right
sys.settrace(sys_trace_probe)
print(sys.gettrace() is sys_trace_probe, sys.call_tracing(sys_call_tracing_probe, (2, 5)), sys.gettrace() is sys_trace_probe)
sys.settrace(None)
sys._settraceallthreads(sys_trace_probe)
print(sys.gettrace() is sys_trace_probe, sys._settraceallthreads(None) is None, sys.gettrace() is None)
try:
    sys.call_tracing(sys_call_tracing_probe, [1, 2])
except TypeError as err:
    print("call-tracing-type", "tuple" in str(err))
for sys_traceprofile_bad_name, sys_traceprofile_bad_call, sys_traceprofile_bad_parts in [
    ("settrace0", lambda: sys.settrace(), ("exactly one argument", "0 given")),
    ("settrace2", lambda: sys.settrace(None, None), ("exactly one argument", "2 given")),
    ("gettrace1", lambda: sys.gettrace(1), ("takes no arguments", "1 given")),
    ("setprofile0", lambda: sys.setprofile(), ("exactly one argument", "0 given")),
    ("setprofile2", lambda: sys.setprofile(None, None), ("exactly one argument", "2 given")),
    ("getprofile1", lambda: sys.getprofile(1), ("takes no arguments", "1 given")),
    ("settraceall0", lambda: sys._settraceallthreads(), ("exactly one argument", "0 given")),
    ("settraceall2", lambda: sys._settraceallthreads(None, None), ("exactly one argument", "2 given")),
    ("setprofileall0", lambda: sys._setprofileallthreads(), ("exactly one argument", "0 given")),
    ("setprofileall2", lambda: sys._setprofileallthreads(None, None), ("exactly one argument", "2 given")),
    ("call_tracing0", lambda: sys.call_tracing(), ("expected 2 arguments", "got 0")),
    ("call_tracing1", lambda: sys.call_tracing(sys_call_tracing_probe), ("expected 2 arguments", "got 1")),
    ("call_tracing3", lambda: sys.call_tracing(sys_call_tracing_probe, (), 3), ("expected 2 arguments", "got 3")),
]:
    try:
        sys_traceprofile_bad_call()
    except TypeError as err:
        sys_traceprofile_bad_message = str(err)
        print("sys-traceprofile-arity", sys_traceprofile_bad_name, all(part in sys_traceprofile_bad_message for part in sys_traceprofile_bad_parts))
print("sys-traceprofile-docs", "debugger chapter" in sys.settrace.__doc__, "debugger chapter" in sys.gettrace.__doc__, "current interpreter" in sys._settraceallthreads.__doc__, "recursively debug" in sys.call_tracing.__doc__, "profiler" in sys.setprofile.__doc__ and "library manual" in sys.setprofile.__doc__, "profiler chapter" in sys.getprofile.__doc__, "current interpreter" in sys._setprofileallthreads.__doc__)
for sys_setter_keyword_name, sys_setter_keyword_call in [
    ("setrecursionlimit", lambda: sys.setrecursionlimit(limit=old_recursion_limit)),
    ("setswitchinterval", lambda: sys.setswitchinterval(interval=old_switch)),
    ("settrace", lambda: sys.settrace(function=None)),
    ("_settraceallthreads", lambda: sys._settraceallthreads(function=None)),
    ("setprofile", lambda: sys.setprofile(function=None)),
    ("_setprofileallthreads", lambda: sys._setprofileallthreads(function=None)),
    ("call_tracing", lambda: sys.call_tracing(func=sys_call_tracing_probe, args=())),
    ("activate_stack_trampoline", lambda: sys.activate_stack_trampoline(backend="perf")),
]:
    try:
        sys_setter_keyword_call()
    except TypeError as err:
        print("sys-setter-keyword", sys_setter_keyword_name, "takes no keyword arguments" in str(err))
print(sys.exception() is None, sys._getframemodulename() == "__main__", sys._is_gil_enabled() == True)
def sys_frame_module_probe():
    return sys._getframemodulename(1)
print(sys_frame_module_probe(), sys._getframemodulename(9999) is None)
print("sys-getframemodulename-keyword", sys._getframemodulename(depth=0) == "__main__")
print(sys._getframemodulename(False) == "__main__", sys._getframemodulename(True) is None)
print(sys._getframe(-1).f_code.co_name == "<module>", sys._getframe(-2).f_globals["__name__"] == "__main__")
print(sys._getframemodulename(-1) == "__main__", sys._getframemodulename(-2) == "__main__")
try:
    sys._getframe(True)
except ValueError as err:
    print("sys-getframe-bool-depth", "not deep enough" in str(err))
try:
    sys._getframe(9999)
except ValueError as err:
    print("sys-getframe-depth", "not deep enough" in str(err))
try:
    sys._getframemodulename("x")
except TypeError as err:
    print("sys-getframemodulename-type", "int" in str(err) or "integer" in str(err))
for sys_getframe_bad_name, sys_getframe_bad_call, sys_getframe_bad_parts in [
    ("frame-type", lambda: sys._getframe("x"), ("str", "cannot be interpreted as an integer")),
    ("module-type", lambda: sys._getframemodulename(None), ("NoneType", "cannot be interpreted as an integer")),
    ("frame-extra", lambda: sys._getframe(0, 1), ("_getframe", "at most 1 argument", "got 2")),
    ("module-extra", lambda: sys._getframemodulename(0, 1), ("_getframemodulename", "at most 1 argument", "2 given")),
]:
    try:
        sys_getframe_bad_call()
    except TypeError as err:
        sys_getframe_bad_message = str(err)
        print("sys-getframe-diagnostic", sys_getframe_bad_name, all(part in sys_getframe_bad_message for part in sys_getframe_bad_parts))
try:
    sys._getframe(depth=0)
except TypeError as err:
    print("sys-getframe-keyword", "takes no keyword arguments" in str(err))
for sys_getframemodulename_bad_name, sys_getframemodulename_bad_call, sys_getframemodulename_bad_parts in [
    ("unexpected", lambda: sys._getframemodulename(x=0), ("unexpected keyword argument", "x")),
    ("duplicate", lambda: sys._getframemodulename(0, depth=0), ("at most 1 argument", "2 given")),
    ("extra-keyword", lambda: sys._getframemodulename(depth=0, x=1), ("at most 1 keyword argument", "2 given")),
]:
    try:
        sys_getframemodulename_bad_call()
    except TypeError as err:
        sys_getframemodulename_bad_message = str(err)
        print("sys-getframemodulename-keyword-diagnostic", sys_getframemodulename_bad_name, all(part in sys_getframemodulename_bad_message for part in sys_getframemodulename_bad_parts))
try:
    sys._is_gil_enabled(1)
except TypeError as err:
    print("sys-gil-args", "argument" in str(err))
audit_events = []
def sys_audit_probe(event, args):
    audit_events.append((event, args))
try:
    sys.audit()
except TypeError as err:
    print("sys-audit-arity", "at least 1 argument" in str(err), "got 0" in str(err))
try:
    sys.audit(1)
except TypeError as err:
    print("sys-audit-event-type", "argument 1 must be str" in str(err), "int" in str(err))
for sys_audit_bad_event, sys_audit_bad_type_name in [(b"x", "bytes"), (None, "None")]:
    try:
        sys.audit(sys_audit_bad_event)
    except TypeError as err:
        sys_audit_bad_event_message = str(err)
        print("sys-audit-event-type", "argument 1 must be str" in sys_audit_bad_event_message, sys_audit_bad_type_name in sys_audit_bad_event_message)
try:
    sys.addaudithook()
except TypeError as err:
    print("sys-addaudithook-arity", "missing required argument" in str(err), "hook" in str(err))
try:
    sys.addaudithook(sys_audit_probe, sys_audit_probe)
except TypeError as err:
    print("sys-addaudithook-arity", "at most 1 argument" in str(err), "2 given" in str(err))
sys.addaudithook(sys_audit_probe)
print(sys.audit("xlang3.fixture", 1, "a") is None, audit_events[0][0], audit_events[0][1])
print(sys.addaudithook(hook=sys_audit_probe) is None)
sys.audit("xlang3.fixture.kw", "k")
print("sys-audit-keyword-hook", audit_events[-2:] == [("xlang3.fixture.kw", ("k",)), ("xlang3.fixture.kw", ("k",))])
for sys_audit_keyword_bad_name, sys_audit_keyword_bad_call, sys_audit_keyword_bad_parts in [
    ("addaudit-unexpected", lambda: sys.addaudithook(callback=sys_audit_probe), ("missing required argument", "hook")),
    ("addaudit-extra-kw", lambda: sys.addaudithook(hook=sys_audit_probe, callback=sys_audit_probe), ("at most 1 keyword argument", "2 given")),
    ("audit-keyword", lambda: sys.audit(event="xlang3.fixture.kw"), ("takes no keyword arguments",)),
]:
    try:
        sys_audit_keyword_bad_call()
    except TypeError as err:
        print("sys-audit-keyword-diagnostic", sys_audit_keyword_bad_name, all(part in str(err) for part in sys_audit_keyword_bad_parts))
print(sys.addaudithook(42) is None)
try:
    sys.audit("xlang3.fixture.bad-hook")
except TypeError as err:
    print("sys-audit-bad-hook", "callable" in str(err) or "call" in str(err))
import builtins
def sys_frame_probe():
    frame = sys._getframe()
    caller = sys._getframe(1)
    return frame.f_code.co_name, caller.f_code.co_name, frame.f_globals["__name__"], frame.f_locals["frame"] is frame

print(sys_frame_probe())
current_frames = sys._current_frames()
current_exceptions = sys._current_exceptions()
current_thread_id = list(current_frames)[0]
print(len(current_frames), len(current_exceptions), current_thread_id in current_exceptions, current_frames[current_thread_id].f_code.co_name == "<module>", current_exceptions[current_thread_id] is None)
print(sys._getframe().f_builtins["len"] is builtins.len, "Exception" in sys._getframe().f_builtins, current_frames[current_thread_id].f_builtins["print"] is builtins.print)
print("sys-frame-helper-docs", "internal and specialized purposes" in sys._getframe.__doc__, "library module" in sys._getframemodulename.__doc__, "specialized purposes only" in sys._current_frames.__doc__, "specialized purposes only" in sys._current_exceptions.__doc__)
print(current_thread_id == threading.get_ident(), "sys" in sys.stdlib_module_names, "threading" in sys.stdlib_module_names, len(sys.stdlib_module_names) > len(sys.builtin_module_names))
print(isinstance(sys.stdlib_module_names, frozenset), "asyncio" in sys.stdlib_module_names, "email" in sys.stdlib_module_names, "encodings" in sys.stdlib_module_names, "tomllib" in sys.stdlib_module_names, "site-packages" in sys.stdlib_module_names)
os.environ["PYTHONBREAKPOINT"] = "0"
print(sys.breakpointhook() is None, sys.breakpointhook(1, label="fixture") is None, sys.__breakpointhook__(flag=True) is None)
sys_frame_thread_ready = threading.Event()
sys_frame_thread_release = threading.Event()
sys_frame_thread_ident = []
def sys_frame_thread_worker():
    sys_frame_thread_ident.append(threading.get_ident())
    sys_frame_thread_ready.set()
    sys_frame_thread_release.wait()

sys_frame_thread = threading.Thread(target=sys_frame_thread_worker)
sys_frame_thread.start()
sys_frame_thread_ready.wait()
multi_thread_frames = sys._current_frames()
print(len(multi_thread_frames) >= 2, threading.get_ident() in multi_thread_frames, sys_frame_thread_ident[0] in multi_thread_frames, multi_thread_frames[sys_frame_thread_ident[0]].f_code.co_name == "sys_frame_thread_worker")
sys_frame_thread_release.set()
sys_frame_thread.join()
sys_exception_thread_ready = threading.Event()
sys_exception_thread_release = threading.Event()
sys_exception_thread_ident = []
sys_exception_thread_error = []
def sys_exception_thread_worker():
    sys_exception_thread_ident.append(threading.get_ident())
    try:
        raise ValueError("thread-active")
    except ValueError as err:
        sys_exception_thread_error.append(err)
        sys_exception_thread_ready.set()
        sys_exception_thread_release.wait()

sys_exception_thread = threading.Thread(target=sys_exception_thread_worker)
sys_exception_thread.start()
sys_exception_thread_ready.wait()
multi_thread_exceptions = sys._current_exceptions()
print(len(multi_thread_exceptions) >= 2, threading.get_ident() in multi_thread_exceptions, sys_exception_thread_ident[0] in multi_thread_exceptions, multi_thread_exceptions[sys_exception_thread_ident[0]] is sys_exception_thread_error[0])
sys_exception_thread_release.set()
sys_exception_thread.join()
class SysClearDescriptorsProbe:
    pass
sys_dump_tracelets_path = "xlang3_sys_tracelets.dot"
if os.path.exists(sys_dump_tracelets_path):
    os.remove(sys_dump_tracelets_path)
print(sys._clear_internal_caches() is None, sys._clear_type_cache() is None, sys._clear_type_descriptors(SysClearDescriptorsProbe) is None, sys._get_cpu_count_config(), sys.is_remote_debug_enabled(), sys.get_coroutine_origin_tracking_depth())
for sys_noarg_keyword_name, sys_noarg_keyword_probe in (
    ("exc_info", sys.exc_info),
    ("exception", sys.exception),
    ("getdefaultencoding", sys.getdefaultencoding),
    ("getfilesystemencoding", sys.getfilesystemencoding),
    ("getfilesystemencodeerrors", sys.getfilesystemencodeerrors),
    ("getrecursionlimit", sys.getrecursionlimit),
    ("gettrace", sys.gettrace),
    ("getprofile", sys.getprofile),
    ("getswitchinterval", sys.getswitchinterval),
    ("get_int_max_str_digits", sys.get_int_max_str_digits),
    ("is_finalizing", sys.is_finalizing),
    ("_clear_internal_caches", sys._clear_internal_caches),
    ("_clear_type_cache", sys._clear_type_cache),
    ("_get_cpu_count_config", sys._get_cpu_count_config),
    ("is_remote_debug_enabled", sys.is_remote_debug_enabled),
    ("_is_gil_enabled", sys._is_gil_enabled),
    ("deactivate_stack_trampoline", sys.deactivate_stack_trampoline),
    ("is_stack_trampoline_active", sys.is_stack_trampoline_active),
    ("getwindowsversion", sys.getwindowsversion),
    ("_enablelegacywindowsfsencoding", sys._enablelegacywindowsfsencoding),
    ("_debugmallocstats", sys._debugmallocstats),
    ("get_coroutine_origin_tracking_depth", sys.get_coroutine_origin_tracking_depth),
    ("get_asyncgen_hooks", sys.get_asyncgen_hooks),
    ("_current_frames", sys._current_frames),
    ("_current_exceptions", sys._current_exceptions),
    ("_jit.is_available", sys._jit.is_available),
    ("_jit.is_enabled", sys._jit.is_enabled),
    ("_jit.is_active", sys._jit.is_active),
):
    try:
        sys_noarg_keyword_probe(x=1)
    except TypeError as err:
        print("sys-noarg-keyword", sys_noarg_keyword_name, "takes no keyword arguments" in str(err))
print(sys._dump_tracelets(sys_dump_tracelets_path) is None, os.path.exists(sys_dump_tracelets_path))
with open(sys_dump_tracelets_path, "rb") as sys_dump_tracelets_file:
    sys_dump_tracelets_data = sys_dump_tracelets_file.read()
print(sys_dump_tracelets_data.startswith(b"digraph ideal"), b'rankdir = "LR"' in sys_dump_tracelets_data, len(sys_dump_tracelets_data) > 0)
os.remove(sys_dump_tracelets_path)
class SysDumpTraceletsPath:
    def __fspath__(self):
        return sys_dump_tracelets_path
print(sys._dump_tracelets(SysDumpTraceletsPath()) is None, os.path.exists(sys_dump_tracelets_path))
os.remove(sys_dump_tracelets_path)
sys_dump_tracelets_bytes_path = bytes(sys_dump_tracelets_path, "utf-8")
print(sys._dump_tracelets(sys_dump_tracelets_bytes_path) is None, os.path.exists(sys_dump_tracelets_path))
os.remove(sys_dump_tracelets_path)
print(sys._dump_tracelets(outpath=sys_dump_tracelets_path) is None, os.path.exists(sys_dump_tracelets_path))
os.remove(sys_dump_tracelets_path)
for sys_dump_tracelets_bad_name, sys_dump_tracelets_bad_call, sys_dump_tracelets_bad_parts in [
    ("missing", lambda: sys._dump_tracelets(), ("missing required argument", "outpath")),
    ("extra", lambda: sys._dump_tracelets(sys_dump_tracelets_path, sys_dump_tracelets_path), ("takes at most 1 argument", "2 given")),
    ("duplicate-keyword", lambda: sys._dump_tracelets(sys_dump_tracelets_path, outpath=sys_dump_tracelets_path), ("takes at most 1 argument", "2 given")),
    ("extra-keyword", lambda: sys._dump_tracelets(outpath=sys_dump_tracelets_path, path=sys_dump_tracelets_path), ("takes at most 1 keyword argument", "2 given")),
    ("unexpected-keyword", lambda: sys._dump_tracelets(path=sys_dump_tracelets_path), ("missing required argument", "outpath")),
    ("type", lambda: sys._dump_tracelets(42), ("expected str, bytes or os.PathLike object", "int")),
]:
    try:
        sys_dump_tracelets_bad_call()
    except TypeError as err:
        sys_dump_tracelets_bad_message = str(err)
        print("sys-dump-tracelets-diagnostic", sys_dump_tracelets_bad_name, all(part in sys_dump_tracelets_bad_message for part in sys_dump_tracelets_bad_parts))
try:
    sys._clear_type_descriptors()
except TypeError as err:
    print("sys-clear-type-descriptors-arity", "takes exactly one argument" in str(err), "0 given" in str(err))
try:
    sys._clear_type_descriptors(SysClearDescriptorsProbe, SysClearDescriptorsProbe)
except TypeError as err:
    print("sys-clear-type-descriptors-arity", "takes exactly one argument" in str(err), "2 given" in str(err))
try:
    sys._clear_type_descriptors(type=SysClearDescriptorsProbe)
except TypeError as err:
    print("sys-clear-type-descriptors-keyword", "takes no keyword arguments" in str(err))
try:
    sys._clear_type_descriptors(42)
except TypeError as err:
    print("sys-clear-type-descriptors-type", "argument must be type" in str(err), "int" in str(err))
try:
    sys._clear_type_descriptors(int)
except TypeError as err:
    print("sys-clear-type-descriptors-immutable", "immutable" in str(err))
sys.set_coroutine_origin_tracking_depth(2)
print(sys.get_coroutine_origin_tracking_depth())
sys.set_coroutine_origin_tracking_depth(0)
print(sys.get_coroutine_origin_tracking_depth())
print(sys.set_coroutine_origin_tracking_depth(True) is None, sys.get_coroutine_origin_tracking_depth())
print(sys.set_coroutine_origin_tracking_depth(False) is None, sys.get_coroutine_origin_tracking_depth())
print("coroutine-origin-depth-keyword", sys.set_coroutine_origin_tracking_depth(depth=True) is None, sys.get_coroutine_origin_tracking_depth())
sys.set_coroutine_origin_tracking_depth(0)
try:
    sys.set_coroutine_origin_tracking_depth(-1)
except ValueError as err:
    print("coroutine-origin-depth", ">= 0" in str(err))
for sys_coroutine_origin_bad_name, sys_coroutine_origin_bad_call, sys_coroutine_origin_bad_parts in [
    ("missing", lambda: sys.set_coroutine_origin_tracking_depth(), ("missing required argument", "depth")),
    ("extra", lambda: sys.set_coroutine_origin_tracking_depth(1, 2), ("takes at most 1 argument", "2 given")),
    ("type", lambda: sys.set_coroutine_origin_tracking_depth("x"), ("str", "cannot be interpreted as an integer")),
    ("keyword-missing", lambda: sys.set_coroutine_origin_tracking_depth(x=0), ("missing required argument", "depth")),
    ("keyword-duplicate", lambda: sys.set_coroutine_origin_tracking_depth(0, depth=1), ("takes at most 1 argument", "2 given")),
    ("keyword-extra", lambda: sys.set_coroutine_origin_tracking_depth(depth=0, x=1), ("takes at most 1 keyword argument", "2 given")),
]:
    try:
        sys_coroutine_origin_bad_call()
    except TypeError as err:
        sys_coroutine_origin_bad_message = str(err)
        print("coroutine-origin-diagnostic", sys_coroutine_origin_bad_name, all(part in sys_coroutine_origin_bad_message for part in sys_coroutine_origin_bad_parts))
asyncgen_hooks = sys.get_asyncgen_hooks()
print(type(asyncgen_hooks).__name__, len(asyncgen_hooks), asyncgen_hooks.firstiter is None, asyncgen_hooks.finalizer is None)
print(type(asyncgen_hooks).__module__, type(asyncgen_hooks).__match_args__)
print(isinstance(asyncgen_hooks, tuple), asyncgen_hooks.count(None), asyncgen_hooks.index(None))
def asyncgen_firstiter_probe(generator):
    return None
def asyncgen_finalizer_probe(generator):
    return None
print(sys.set_asyncgen_hooks(asyncgen_firstiter_probe, asyncgen_finalizer_probe) is None)
asyncgen_hooks = sys.get_asyncgen_hooks()
print(asyncgen_hooks.firstiter is asyncgen_firstiter_probe, asyncgen_hooks.finalizer is asyncgen_finalizer_probe, asyncgen_hooks[0] is asyncgen_firstiter_probe, type(asyncgen_hooks).firstiter.__name__)
print(sys.set_asyncgen_hooks(finalizer=None) is None, sys.get_asyncgen_hooks().firstiter is asyncgen_firstiter_probe, sys.get_asyncgen_hooks().finalizer is None)
print(sys.set_asyncgen_hooks(firstiter=None) is None, sys.get_asyncgen_hooks().firstiter is None, sys.get_asyncgen_hooks().finalizer is None)
print(sys.set_asyncgen_hooks(firstiter=asyncgen_firstiter_probe, finalizer=asyncgen_finalizer_probe) is None)
asyncgen_hooks = sys.get_asyncgen_hooks()
print("asyncgen-hooks-keyword", asyncgen_hooks.firstiter is asyncgen_firstiter_probe, asyncgen_hooks.finalizer is asyncgen_finalizer_probe)
sys.set_asyncgen_hooks(firstiter=None, finalizer=None)
try:
    sys.set_asyncgen_hooks(42)
except TypeError as err:
    print("asyncgen-hooks", "firstiter" in str(err), "callable" in str(err), "int" in str(err))
try:
    sys.set_asyncgen_hooks(None, 42)
except TypeError as err:
    print("asyncgen-hooks", "finalizer" in str(err), "callable" in str(err), "int" in str(err))
for sys_asyncgen_bad_name, sys_asyncgen_bad_call, sys_asyncgen_bad_parts in [
    ("extra", lambda: sys.set_asyncgen_hooks(None, None, None), ("at most 2 arguments", "3 given")),
    ("keyword", lambda: sys.set_asyncgen_hooks(unknown=None), ("unexpected keyword argument", "unknown")),
    ("duplicate", lambda: sys.set_asyncgen_hooks(None, firstiter=None), ("given by name", "firstiter", "position (1)")),
    ("too-many-keywords", lambda: sys.set_asyncgen_hooks(firstiter=None, finalizer=None, unknown=None), ("at most 2 keyword arguments", "3 given")),
    ("keyword-firstiter-type", lambda: sys.set_asyncgen_hooks(firstiter=42), ("firstiter", "callable", "int")),
    ("keyword-finalizer-type", lambda: sys.set_asyncgen_hooks(finalizer=42), ("finalizer", "callable", "int")),
]:
    try:
        sys_asyncgen_bad_call()
    except TypeError as err:
        sys_asyncgen_bad_message = str(err)
        print("asyncgen-hooks-diagnostic", sys_asyncgen_bad_name, all(part in sys_asyncgen_bad_message for part in sys_asyncgen_bad_parts))
sys_noarg_typeerror_probes = [
    sys._current_frames,
    sys._current_exceptions,
    sys._get_cpu_count_config,
    sys._clear_internal_caches,
    sys._clear_type_cache,
    sys.getdefaultencoding,
    sys.getfilesystemencoding,
    sys.getfilesystemencodeerrors,
    sys.getrecursionlimit,
    sys.getallocatedblocks,
    sys.getswitchinterval,
    sys.get_int_max_str_digits,
    sys.is_finalizing,
    sys.is_remote_debug_enabled,
    sys._is_gil_enabled,
    sys.deactivate_stack_trampoline,
    sys.is_stack_trampoline_active,
    sys._jit.is_available,
    sys._jit.is_enabled,
    sys._jit.is_active,
    sys._debugmallocstats,
    sys.get_coroutine_origin_tracking_depth,
    sys.get_asyncgen_hooks,
]
if hasattr(sys, "getwindowsversion"):
    sys_noarg_typeerror_probes.append(sys.getwindowsversion)
if hasattr(sys, "_enablelegacywindowsfsencoding"):
    sys_noarg_typeerror_probes.append(sys._enablelegacywindowsfsencoding)
sys_runtime_metadata_names = [
    "_current_frames",
    "_current_exceptions",
    "_get_cpu_count_config",
    "_getframe",
    "exc_info",
    "exception",
    "exit",
    "displayhook",
    "excepthook",
    "unraisablehook",
    "breakpointhook",
    "addaudithook",
    "audit",
    "settrace",
    "gettrace",
    "_settraceallthreads",
    "call_tracing",
    "setprofile",
    "getprofile",
    "_setprofileallthreads",
    "getdefaultencoding",
    "getfilesystemencoding",
    "getfilesystemencodeerrors",
    "getrecursionlimit",
    "setrecursionlimit",
    "intern",
    "_is_interned",
    "getunicodeinternedsize",
    "_is_immortal",
    "getsizeof",
    "getrefcount",
    "getallocatedblocks",
    "getswitchinterval",
    "setswitchinterval",
    "get_int_max_str_digits",
    "set_int_max_str_digits",
    "is_finalizing",
    "activate_stack_trampoline",
    "deactivate_stack_trampoline",
    "is_stack_trampoline_active",
    "_debugmallocstats",
    "_dump_tracelets",
    "is_remote_debug_enabled",
    "_is_gil_enabled",
    "_getframemodulename",
    "_clear_internal_caches",
    "_clear_type_cache",
    "_clear_type_descriptors",
    "get_coroutine_origin_tracking_depth",
    "set_coroutine_origin_tracking_depth",
    "get_asyncgen_hooks",
    "set_asyncgen_hooks",
]
if hasattr(sys, "getwindowsversion"):
    sys_runtime_metadata_names.append("getwindowsversion")
if hasattr(sys, "_enablelegacywindowsfsencoding"):
    sys_runtime_metadata_names.append("_enablelegacywindowsfsencoding")
print(
    all(getattr(sys, name).__name__ == name and getattr(sys, name).__qualname__ == name for name in sys_runtime_metadata_names),
    all(getattr(sys, name).__module__ == "sys" for name in sys_runtime_metadata_names),
    all(isinstance(getattr(sys, name).__doc__, str) and len(getattr(sys, name).__doc__) > 0 for name in sys_runtime_metadata_names),
)
sys_runtime_text_signatures = {
    "exc_info": "($module, /)",
    "exception": "($module, /)",
    "exit": "($module, status=None, /)",
    "displayhook": "($module, object, /)",
    "__displayhook__": "($module, object, /)",
    "excepthook": "($module, exctype, value, traceback, /)",
    "__excepthook__": "($module, exctype, value, traceback, /)",
    "unraisablehook": "($module, unraisable, /)",
    "__unraisablehook__": "($module, unraisable, /)",
    "breakpointhook": "($module, /, *args, **kwargs)",
    "__breakpointhook__": "($module, /, *args, **kwargs)",
    "addaudithook": "($module, /, hook)",
    "audit": "($module, event, /, *args)",
    "settrace": "($module, function, /)",
    "gettrace": "($module, /)",
    "_settraceallthreads": "($module, function, /)",
    "call_tracing": "($module, func, args, /)",
    "setprofile": "($module, function, /)",
    "getprofile": "($module, /)",
    "_setprofileallthreads": "($module, function, /)",
    "_getframe": "($module, depth=0, /)",
    "_getframemodulename": "($module, /, depth=0)",
    "_current_frames": "($module, /)",
    "_current_exceptions": "($module, /)",
    "_clear_internal_caches": "($module, /)",
    "_clear_type_cache": "($module, /)",
    "_clear_type_descriptors": "($module, type, /)",
    "getdefaultencoding": "($module, /)",
    "getfilesystemencoding": "($module, /)",
    "getfilesystemencodeerrors": "($module, /)",
    "getrecursionlimit": "($module, /)",
    "setrecursionlimit": "($module, limit, /)",
    "getswitchinterval": "($module, /)",
    "setswitchinterval": "($module, interval, /)",
    "get_int_max_str_digits": "($module, /)",
    "set_int_max_str_digits": "($module, /, maxdigits)",
    "is_finalizing": "($module, /)",
    "activate_stack_trampoline": "($module, backend, /)",
    "deactivate_stack_trampoline": "($module, /)",
    "is_stack_trampoline_active": "($module, /)",
    "_debugmallocstats": "($module, /)",
    "_get_cpu_count_config": "($module, /)",
    "_dump_tracelets": "($module, /, outpath)",
    "get_coroutine_origin_tracking_depth": "($module, /)",
    "set_coroutine_origin_tracking_depth": "($module, /, depth)",
    "get_asyncgen_hooks": "($module, /)",
    "is_remote_debug_enabled": "($module, /)",
    "_is_gil_enabled": "($module, /)",
}
if hasattr(sys, "getwindowsversion"):
    sys_runtime_text_signatures["getwindowsversion"] = "($module, /)"
if hasattr(sys, "_enablelegacywindowsfsencoding"):
    sys_runtime_text_signatures["_enablelegacywindowsfsencoding"] = "($module, /)"
print("sys-runtime-text-signatures", all(getattr(sys, name).__text_signature__ == signature for name, signature in sys_runtime_text_signatures.items()))
sys_noarg_typeerror_count = 0
for sys_noarg_typeerror_probe in sys_noarg_typeerror_probes:
    try:
        sys_noarg_typeerror_probe(1)
    except TypeError as err:
        if "argument" in str(err) or "expected 0" in str(err) or "takes no arguments" in str(err):
            sys_noarg_typeerror_count += 1
print("sys-noarg-typeerrors", sys_noarg_typeerror_count, len(sys_noarg_typeerror_probes))
for sys_noarg_name, sys_noarg_probe in (
    ("allocated", sys.getallocatedblocks),
    ("encoding", sys.getdefaultencoding),
    ("cpu", sys._get_cpu_count_config),
    ("jit", sys._jit.is_available),
):
    try:
        sys_noarg_probe(1)
    except TypeError as err:
        print("sys-noarg-diagnostic", sys_noarg_name, "takes no arguments" in str(err), "1 given" in str(err))
try:
    sys.getunicodeinternedsize(1)
except TypeError as err:
    print("sys-noarg-diagnostic", "unicodeinterned", "no positional arguments" in str(err))
try:
    raise RuntimeError("active")
except RuntimeError as err:
    print(sys.exception() is err, sys.exc_info()[1] is err)
    print(sys._current_exceptions()[current_thread_id] is err)
for sys_exception_state_name, sys_exception_state_call in [
    ("exc_info", lambda: sys.exc_info(1)),
    ("exception", lambda: sys.exception(1)),
]:
    try:
        sys_exception_state_call()
    except TypeError as err:
        sys_exception_state_message = str(err)
        print("sys-exception-state-arity", sys_exception_state_name, "takes no arguments" in sys_exception_state_message, "1 given" in sys_exception_state_message)
try:
    sys.exit()
except SystemExit as err:
    print("sys-exit-empty", err.code is None, str(err) == "", err.args == ())
try:
    sys.exit(5)
except SystemExit as err:
    print("sys-exit-code", err.code, str(err), err.args == (5,))
for sys_exit_tuple_status in [(), ("x",), ("x", "y")]:
    try:
        sys.exit(sys_exit_tuple_status)
    except SystemExit as err:
        if len(sys_exit_tuple_status) == 0:
            expected_exit_code = None
        elif len(sys_exit_tuple_status) == 1:
            expected_exit_code = sys_exit_tuple_status[0]
        else:
            expected_exit_code = sys_exit_tuple_status
        print(
            "sys-exit-tuple",
            len(sys_exit_tuple_status),
            err.code == expected_exit_code,
            err.args == sys_exit_tuple_status,
        )
sys_exit_multi = SystemExit(1, 2)
print("system-exit-constructor", str(SystemExit()) == "", str(SystemExit(None)) == "None", sys_exit_multi.code == (1, 2), str(sys_exit_multi) == "(1, 2)")
try:
    sys.exit(1, 2)
except TypeError as err:
    print("sys-exit-arity", "at most 1 argument" in str(err), "got 2" in str(err))
try:
    sys.exit(code=7)
except TypeError as err:
    print("sys-exit-keyword", "takes no keyword arguments" in str(err))
class SysHookCapture:
    def __init__(self):
        self.items = []

    def write(self, text):
        self.items.append(text)
        return len(text)

    def flush(self):
        return None

saved_stdout = sys.stdout
saved_stderr = sys.stderr
saved_builtin_underscore = getattr(builtins, "_", None)
sys_hook_stdout = SysHookCapture()
sys_hook_stderr = SysHookCapture()
try:
    sys.stdout = sys_hook_stdout
    sys.stderr = sys_hook_stderr
    sys.displayhook(42)
    sys.__displayhook__("hook\ntext")
    sys.displayhook(None)
    sys.excepthook(ValueError, ValueError("hooked"), None)
    sys.__excepthook__(RuntimeError, RuntimeError("defaulted"), None)
    sys.unraisablehook(object())
    sys.__unraisablehook__(object())
finally:
    sys.stdout = saved_stdout
    sys.stderr = saved_stderr
    builtins._ = saved_builtin_underscore
print(
    len(sys_hook_stdout.items),
    sys_hook_stdout.items[0].strip(),
    sys_hook_stdout.items[1].strip(),
    len(sys_hook_stderr.items),
    "ValueError: hooked" in sys_hook_stderr.items[0],
    "RuntimeError: defaulted" in sys_hook_stderr.items[1],
    builtins._ is saved_builtin_underscore,
)
for sys_hook_bad_name, sys_hook_bad_call, sys_hook_bad_parts in [
    ("displayhook", lambda: sys.displayhook(), ("exactly one argument", "0 given")),
    ("__displayhook__", lambda: sys.__displayhook__(), ("exactly one argument", "0 given")),
    ("excepthook", lambda: sys.excepthook(ValueError), ("expected 3 arguments", "got 1")),
    ("__excepthook__", lambda: sys.__excepthook__(ValueError), ("expected 3 arguments", "got 1")),
    ("unraisablehook", lambda: sys.unraisablehook(), ("exactly one argument", "0 given")),
    ("__unraisablehook__", lambda: sys.__unraisablehook__(), ("exactly one argument", "0 given")),
]:
    try:
        sys_hook_bad_call()
    except TypeError as err:
        sys_hook_bad_message = str(err)
        print("sys-hook-type", sys_hook_bad_name, all(part in sys_hook_bad_message for part in sys_hook_bad_parts))
for sys_hook_keyword_name, sys_hook_keyword_call in [
    ("displayhook", lambda: sys.displayhook(object=42)),
    ("__displayhook__", lambda: sys.__displayhook__(object=42)),
    ("excepthook", lambda: sys.excepthook(type=ValueError, value=ValueError("hooked"), traceback=None)),
    ("__excepthook__", lambda: sys.__excepthook__(type=ValueError, value=ValueError("hooked"), traceback=None)),
    ("unraisablehook", lambda: sys.unraisablehook(unraisable=object())),
    ("__unraisablehook__", lambda: sys.__unraisablehook__(unraisable=object())),
]:
    try:
        sys_hook_keyword_call()
    except TypeError as err:
        print("sys-hook-keyword", sys_hook_keyword_name, "takes no keyword arguments" in str(err))
print("sys-hook-docs", "exc_type: Exception type." in sys.unraisablehook.__doc__, sys.__unraisablehook__.__doc__ is sys.unraisablehook.__doc__, sys.breakpointhook.__doc__.endswith("\n"), sys.__breakpointhook__.__doc__ is sys.breakpointhook.__doc__)
print("sys-exception-exit-docs", "older stack frame" in sys.exc_info.__doc__, "if no such exception exists" in sys.exception.__doc__, "If the status is omitted or None" in sys.exit.__doc__, "exit status will be one" in sys.exit.__doc__)
sys_debugmalloc_stderr = SysHookCapture()
try:
    sys.stderr = sys_debugmalloc_stderr
    debugmalloc_result = sys._debugmallocstats()
finally:
    sys.stderr = saved_stderr
print(
    debugmalloc_result is None,
    len(sys_debugmalloc_stderr.items) == 1,
    sys_debugmalloc_stderr.items[0].startswith("XLang3 allocator stats\nobject_blocks="),
    "bucket_blocks=" in sys_debugmalloc_stderr.items[0],
    "large_blocks=" in sys_debugmalloc_stderr.items[0],
)
clock_info = time.get_clock_info("monotonic")
print(clock_info.monotonic, clock_info.adjustable, clock_info.resolution > 0, isinstance(clock_info.implementation, str))
clock_info_repr = repr(clock_info)
print(
    type(clock_info).__name__,
    type(clock_info).__module__,
    type(clock_info).__qualname__,
    type(clock_info).__doc__ == "A simple attribute-based namespace.",
    clock_info_repr.startswith("namespace("),
    "monotonic=True" in clock_info_repr,
    "adjustable=False" in clock_info_repr,
    "resolution=" in clock_info_repr,
)
time_function_names = (
    "time",
    "time_ns",
    "monotonic",
    "monotonic_ns",
    "perf_counter",
    "perf_counter_ns",
    "process_time",
    "process_time_ns",
    "thread_time",
    "thread_time_ns",
    "get_clock_info",
    "sleep",
    "localtime",
    "gmtime",
    "ctime",
    "mktime",
    "strftime",
    "strptime",
    "asctime",
)
print(
    all(getattr(time, name).__name__ == name and getattr(time, name).__qualname__ == name for name in time_function_names),
    all(getattr(time, name).__module__ == "time" for name in time_function_names),
    all(isinstance(getattr(time, name).__doc__, str) and len(getattr(time, name).__doc__) > 0 for name in time_function_names),
)
for clock_info_bad_name, clock_info_bad_call, clock_info_bad_error, clock_info_bad_parts in [
    ("missing", lambda: time.get_clock_info(), TypeError, ("takes exactly 1 argument", "0 given")),
    ("extra", lambda: time.get_clock_info("time", "x"), TypeError, ("takes exactly 1 argument", "2 given")),
    ("keyword", lambda: time.get_clock_info(name="time"), TypeError, ("takes no keyword arguments",)),
    ("type", lambda: time.get_clock_info(1), TypeError, ("argument 1 must be str", "int")),
    ("unknown", lambda: time.get_clock_info("missing"), ValueError, ("unknown clock",)),
]:
    try:
        clock_info_bad_call()
    except clock_info_bad_error as err:
        clock_info_bad_message = str(err)
        print("time-clock-info-diagnostic", clock_info_bad_name, all(part in clock_info_bad_message for part in clock_info_bad_parts))
print(time.process_time() >= 0, time.process_time_ns() >= 0, time.thread_time() >= 0, time.thread_time_ns() >= 0)
for time_noarg_clock in [
    time.time,
    time.time_ns,
    time.monotonic,
    time.monotonic_ns,
    time.perf_counter,
    time.perf_counter_ns,
    time.process_time,
    time.process_time_ns,
    time.thread_time,
    time.thread_time_ns,
]:
    try:
        time_noarg_clock(1)
    except TypeError as err:
        time_noarg_clock_message = str(err)
        print("time-clock-arity", time_noarg_clock.__name__, time_noarg_clock.__name__ in time_noarg_clock_message, "takes no arguments" in time_noarg_clock_message, "1 given" in time_noarg_clock_message)
    try:
        time_noarg_clock(flag=True)
    except TypeError as err:
        time_noarg_clock_message = str(err)
        print("time-clock-keyword", time_noarg_clock.__name__, time_noarg_clock.__name__ in time_noarg_clock_message, "takes no keyword arguments" in time_noarg_clock_message)
print(time.sleep(False) is None)
for time_sleep_bad_name, time_sleep_bad_call, time_sleep_bad_error, time_sleep_bad_parts in [
    ("missing", lambda: time.sleep(), TypeError, ("takes exactly one argument", "0 given")),
    ("extra", lambda: time.sleep(0, 1), TypeError, ("takes exactly one argument", "2 given")),
    ("keyword", lambda: time.sleep(secs=0), TypeError, ("takes no keyword arguments",)),
    ("type", lambda: time.sleep("x"), TypeError, ("str", "cannot be interpreted as an integer or float")),
    ("negative", lambda: time.sleep(-1), ValueError, ("sleep length must be non-negative",)),
]:
    try:
        time_sleep_bad_call()
    except time_sleep_bad_error as err:
        time_sleep_bad_message = str(err)
        print("time-sleep-diagnostic", time_sleep_bad_name, all(part in time_sleep_bad_message for part in time_sleep_bad_parts))
epoch_utc = time.gmtime(0)
print(isinstance(epoch_utc, time.struct_time), epoch_utc.tm_year, epoch_utc.tm_mon, epoch_utc.tm_mday, time.strftime("%Y", epoch_utc))
print(time.mktime(time.localtime(0)) == 0.0, isinstance(time.tzname, tuple), isinstance(time.ctime(0), str))
print(time.gmtime(True).tm_sec, time.gmtime(False).tm_sec, time.localtime(True).tm_sec, time.localtime(False).tm_sec, isinstance(time.ctime(True), str), isinstance(time.ctime(False), str))
for time_timestamp_bad_name, time_timestamp_bad_call, time_timestamp_bad_parts in [
    ("local-extra", lambda: time.localtime(0, 1), ("takes at most 1 argument", "2 given")),
    ("local-keyword", lambda: time.localtime(secs=0), ("takes no keyword arguments",)),
    ("local-type", lambda: time.localtime("x"), ("str", "cannot be interpreted as an integer")),
    ("gmt-extra", lambda: time.gmtime(0, 1), ("takes at most 1 argument", "2 given")),
    ("gmt-keyword", lambda: time.gmtime(secs=0), ("takes no keyword arguments",)),
    ("gmt-type", lambda: time.gmtime("x"), ("str", "cannot be interpreted as an integer")),
    ("ctime-extra", lambda: time.ctime(0, 1), ("takes at most 1 argument", "2 given")),
    ("ctime-keyword", lambda: time.ctime(secs=0), ("takes no keyword arguments",)),
    ("ctime-type", lambda: time.ctime("x"), ("str", "cannot be interpreted as an integer")),
]:
    try:
        time_timestamp_bad_call()
    except TypeError as err:
        time_timestamp_bad_message = str(err)
        print("time-timestamp-diagnostic", time_timestamp_bad_name, all(part in time_timestamp_bad_message for part in time_timestamp_bad_parts))
for time_nonfinite_name, time_nonfinite_call, time_nonfinite_error, time_nonfinite_parts in [
    ("local-nan", lambda: time.localtime(float("nan")), ValueError, ("Invalid value NaN",)),
    ("gmt-inf", lambda: time.gmtime(float("inf")), OverflowError, ("timestamp out of range", "time_t")),
    ("ctime-neginf", lambda: time.ctime(float("-inf")), OverflowError, ("timestamp out of range", "time_t")),
]:
    try:
        time_nonfinite_call()
    except time_nonfinite_error as err:
        time_nonfinite_message = str(err)
        print("time-timestamp-nonfinite", time_nonfinite_name, all(part in time_nonfinite_message for part in time_nonfinite_parts))
time_attr_instance = type("TimeAttrInstance", (), {})()
for time_attr_index, time_attr_name in enumerate(("tm_year", "tm_mon", "tm_mday", "tm_hour", "tm_min", "tm_sec", "tm_wday", "tm_yday", "tm_isdst")):
    setattr(time_attr_instance, time_attr_name, (2026, 1, 2, 3, 4, 5, 6, 7, 8)[time_attr_index])
for time_mktime_bad_name, time_mktime_bad_call, time_mktime_bad_parts in [
    ("missing", lambda: time.mktime(), ("takes exactly one argument", "0 given")),
    ("extra", lambda: time.mktime((1970, 1, 1, 0, 0, 0, 3, 1, -1), 1), ("takes exactly one argument", "2 given")),
    ("keyword", lambda: time.mktime(t=(1970, 1, 1, 0, 0, 0, 3, 1, -1)), ("takes no keyword arguments",)),
    ("type", lambda: time.mktime("x"), ("Tuple or struct_time argument required",)),
    ("list-type", lambda: time.mktime([2026, 1, 2, 3, 4, 5, 6, 7, 8]), ("Tuple or struct_time argument required",)),
    ("attr-instance-type", lambda: time.mktime(time_attr_instance), ("Tuple or struct_time argument required",)),
    ("tuple-short", lambda: time.mktime((1, 2)), ("mktime(): illegal time tuple argument",)),
    ("tuple-long", lambda: time.mktime((2026, 1, 2, 3, 4, 5, 6, 7, 8, 9)), ("mktime(): illegal time tuple argument",)),
    ("field-type", lambda: time.mktime((1970.0, 1, 1, 0, 0, 0, 3, 1, -1)), ("float", "cannot be interpreted as an integer")),
]:
    try:
        time_mktime_bad_call()
    except TypeError as err:
        time_mktime_bad_message = str(err)
        print("time-mktime-diagnostic", time_mktime_bad_name, all(part in time_mktime_bad_message for part in time_mktime_bad_parts))
print(time.asctime((2026, 8, 6, 1, 2, 3, 2, 218, -1)), time.asctime((2026, 8, 16, 1, 2, 3, 6, 228, -1)))
for time_asctime_bad_name, time_asctime_bad_call, time_asctime_bad_parts in [
    ("extra", lambda: time.asctime((2026, 1, 1, 0, 0, 0, 3, 1, -1), "x"), ("asctime expected at most 1 argument", "got 2")),
    ("keyword", lambda: time.asctime(t=(2026, 1, 1, 0, 0, 0, 3, 1, -1)), ("takes no keyword arguments",)),
    ("type", lambda: time.asctime("x"), ("Tuple or struct_time argument required",)),
    ("list-type", lambda: time.asctime([2026, 1, 2, 3, 4, 5, 6, 7, 8]), ("Tuple or struct_time argument required",)),
    ("attr-instance-type", lambda: time.asctime(time_attr_instance), ("Tuple or struct_time argument required",)),
    ("tuple-short", lambda: time.asctime((1, 2)), ("illegal time tuple argument",)),
    ("tuple-long", lambda: time.asctime((2026, 1, 2, 3, 4, 5, 6, 7, 8, 9)), ("illegal time tuple argument",)),
    ("field-type", lambda: time.asctime((2026.0, 1, 1, 0, 0, 0, 3, 1, -1)), ("float", "cannot be interpreted as an integer")),
]:
    try:
        time_asctime_bad_call()
    except TypeError as err:
        time_asctime_bad_message = str(err)
        print("time-asctime-diagnostic", time_asctime_bad_name, all(part in time_asctime_bad_message for part in time_asctime_bad_parts))
asctime_default = time.asctime()
print("time-asctime-default", isinstance(asctime_default, str), len(asctime_default) == 24, asctime_default[3] == " ", asctime_default[7] == " ", asctime_default[13] == ":", asctime_default[16] == ":", asctime_default[19] == " ")
bool_time_tuple = (2026, True, True, False, True, False, 2, True, -1)
print(time.asctime(bool_time_tuple), time.strftime("%Y %m %d %H %M %S %j", bool_time_tuple), time.mktime((1970, True, True, False, False, False, 3, True, -1)) == time.mktime((1970, 1, 1, 0, 0, 0, 3, 1, -1)))
strftime_locale_tuple = (2026, 8, 6, 9, 2, 3, 3, 218, -1)
strftime_locale_tuple_pm = (2026, 8, 6, 21, 2, 3, 3, 218, -1)
print(time.strftime("%c", strftime_locale_tuple), time.strftime("%r", strftime_locale_tuple), time.strftime("%r", strftime_locale_tuple_pm))
for time_strftime_bad_name, time_strftime_bad_call, time_strftime_bad_parts in [
    ("missing", lambda: time.strftime(), ("takes at least 1 argument", "0 given")),
    ("extra", lambda: time.strftime("%Y", strftime_locale_tuple, "x"), ("takes at most 2 arguments", "3 given")),
    ("keyword", lambda: time.strftime(format="%Y", t=strftime_locale_tuple), ("takes no keyword arguments",)),
    ("format-type", lambda: time.strftime(1), ("argument 1 must be str", "int")),
    ("tuple-type", lambda: time.strftime("%Y", "x"), ("Tuple or struct_time argument required",)),
    ("list-type", lambda: time.strftime("%Y", [2026, 1, 2, 3, 4, 5, 6, 7, 8]), ("Tuple or struct_time argument required",)),
    ("attr-instance-type", lambda: time.strftime("%Y", time_attr_instance), ("Tuple or struct_time argument required",)),
    ("tuple-short", lambda: time.strftime("%Y", (1, 2)), ("illegal time tuple argument",)),
    ("tuple-long", lambda: time.strftime("%Y", (2026, 1, 2, 3, 4, 5, 6, 7, 8, 9)), ("illegal time tuple argument",)),
    ("field-type", lambda: time.strftime("%Y", (2026.0, 1, 1, 0, 0, 0, 3, 1, -1)), ("float", "cannot be interpreted as an integer")),
]:
    try:
        time_strftime_bad_call()
    except TypeError as err:
        time_strftime_bad_message = str(err)
        print("time-strftime-diagnostic", time_strftime_bad_name, all(part in time_strftime_bad_message for part in time_strftime_bad_parts))
for bad_strftime_format in ["%f", "%k", "%l", "%P", "%q", "%Q", "%s", "%"]:
    try:
        time.strftime(bad_strftime_format, strftime_locale_tuple)
    except ValueError as err:
        print("strftime-invalid", str(err) == "Invalid format string")
# time structseq behavior and parsing.
lambda_dict_literal = (lambda: {"tm_zone": "LD", "tm_gmtoff": 12})()
lambda_set_literal = (lambda: {"alpha"})()
print("lambda-container-literals", lambda_dict_literal["tm_zone"], lambda_dict_literal["tm_gmtoff"], "alpha" in lambda_set_literal)
print(epoch_utc[0], len(epoch_utc), epoch_utc.n_sequence_fields, list(epoch_utc)[:3])
print(epoch_utc.n_fields, epoch_utc.n_unnamed_fields, epoch_utc.tm_zone == "UTC", epoch_utc.tm_gmtoff == 0)
print(time.struct_time.__module__, time.struct_time.__qualname__, time.struct_time.__doc__ is not None, time.struct_time.n_fields, time.struct_time.n_sequence_fields, time.struct_time.n_unnamed_fields, time.struct_time.tm_zone is not None, time.struct_time.tm_gmtoff is not None)
print(time._STRUCT_TM_ITEMS, time._STRUCT_TM_ITEMS == time.struct_time.n_fields)
print(time.struct_time.__match_args__, time.struct_time.tm_year.__name__, time.struct_time.tm_isdst.__name__)
time_struct_year_descriptor = time.struct_time.tm_year
time_struct_zone_descriptor = time.struct_time.tm_zone
time_struct_descriptor_probe = time.struct_time((2026, 8, 26, 1, 2, 3, 2, 238, -1, "Z", 9))
print(type(time_struct_year_descriptor).__name__, type(time_struct_year_descriptor).__module__, time_struct_year_descriptor.__objclass__ is time.struct_time, time_struct_year_descriptor.__get__(time_struct_descriptor_probe), time_struct_zone_descriptor.__get__(time_struct_descriptor_probe), inspect.ismemberdescriptor(time_struct_year_descriptor), repr(time_struct_year_descriptor) == "<member 'tm_year' of 'time.struct_time' objects>")
constructed_time = time.struct_time((2026, 8, 26, 1, 2, 3, 2, 238, -1))
constructed_zone_time = time.struct_time((2026, 8, 26, 1, 2, 3, 2, 238, -1, "X", 123))
constructed_dict_time = time.struct_time((2026, 8, 26, 1, 2, 3, 2, 238, -1), {"tm_zone": "Y", "tm_gmtoff": 456})
constructed_keyword_time = time.struct_time(sequence=(2026, 8, 26, 1, 2, 3, 2, 238, -1))
constructed_keyword_dict_time = time.struct_time(sequence=(2026, 8, 26, 1, 2, 3, 2, 238, -1), dict={"tm_zone": "KW", "tm_gmtoff": 789})
constructed_new_time = time.struct_time.__new__(time.struct_time, (2026, 8, 26, 1, 2, 3, 2, 238, -1))
constructed_new_keyword_time = time.struct_time.__new__(time.struct_time, sequence=(2026, 8, 26, 1, 2, 3, 2, 238, -1), dict={"tm_zone": "NEW", "tm_gmtoff": 987})
print("struct-time-new", constructed_new_time.tm_year, constructed_new_keyword_time.tm_zone, constructed_new_keyword_time.tm_gmtoff, time.struct_time.__new__.__name__, time.struct_time.__new__.__qualname__, time.struct_time.__new__.__doc__ is not None)
struct_time_getnewargs = constructed_zone_time.__getnewargs__()
print("struct-time-getnewargs", isinstance(struct_time_getnewargs, tuple), len(struct_time_getnewargs), struct_time_getnewargs[0] == tuple(constructed_zone_time), len(struct_time_getnewargs[0]), time.struct_time.__getnewargs__.__name__, time.struct_time.__getnewargs__.__qualname__, getattr(time.struct_time.__getnewargs__, "__module__", None) is None, time.struct_time.__getnewargs__.__doc__ is None)
struct_time_reduce = constructed_zone_time.__reduce__()
struct_time_reduce_ex = constructed_zone_time.__reduce_ex__(4)
print("struct-time-reduce", struct_time_reduce[0] is time.struct_time, struct_time_reduce[1][0] == tuple(constructed_zone_time), struct_time_reduce[1][1]["tm_zone"], struct_time_reduce[1][1]["tm_gmtoff"], struct_time_reduce_ex == struct_time_reduce)
print("struct-time-reduce-metadata", time.struct_time.__reduce__.__name__, time.struct_time.__reduce__.__qualname__, getattr(time.struct_time.__reduce__, "__module__", None) is None, time.struct_time.__reduce__.__doc__ is None, time.struct_time.__reduce_ex__.__name__, time.struct_time.__reduce_ex__.__qualname__, getattr(time.struct_time.__reduce_ex__, "__module__", None) is None, time.struct_time.__reduce_ex__.__doc__ is not None)
for struct_time_getnewargs_bad_name, struct_time_getnewargs_bad_call, struct_time_getnewargs_bad_parts in [
    ("extra", lambda: constructed_time.__getnewargs__(1), ("takes no arguments", "1 given")),
    ("keyword", lambda: constructed_time.__getnewargs__(x=1), ("takes no keyword arguments",)),
    ("unbound-missing", lambda: time.struct_time.__getnewargs__(), ("needs an argument",)),
    ("receiver", lambda: time.struct_time.__getnewargs__([]), ("descriptor '__getnewargs__'", "list")),
]:
    try:
        struct_time_getnewargs_bad_call()
    except TypeError as err:
        struct_time_getnewargs_bad_message = str(err)
        print("struct-time-getnewargs-diagnostic", struct_time_getnewargs_bad_name, all(part in struct_time_getnewargs_bad_message for part in struct_time_getnewargs_bad_parts))
for struct_time_reduce_bad_name, struct_time_reduce_bad_call, struct_time_reduce_bad_parts in [
    ("reduce-extra", lambda: constructed_time.__reduce__(1), ("takes no arguments", "1 given")),
    ("reduce-keyword", lambda: constructed_time.__reduce__(x=1), ("takes no keyword arguments",)),
    ("reduce-unbound-missing", lambda: time.struct_time.__reduce__(), ("needs an argument",)),
    ("reduce-receiver", lambda: time.struct_time.__reduce__([]), ("descriptor '__reduce__'", "list")),
    ("reduce-ex-missing-proto", lambda: constructed_time.__reduce_ex__(), ("exactly one argument", "0 given")),
    ("reduce-ex-extra", lambda: constructed_time.__reduce_ex__(4, 5), ("exactly one argument", "2 given")),
    ("reduce-ex-keyword", lambda: constructed_time.__reduce_ex__(proto=4), ("takes no keyword arguments",)),
    ("reduce-ex-type", lambda: constructed_time.__reduce_ex__("x"), ("str", "cannot be interpreted as an integer")),
]:
    try:
        struct_time_reduce_bad_call()
    except TypeError as err:
        struct_time_reduce_bad_message = str(err)
        print("struct-time-reduce-diagnostic", struct_time_reduce_bad_name, all(part in struct_time_reduce_bad_message for part in struct_time_reduce_bad_parts))
for struct_time_new_bad_name, struct_time_new_bad_call, struct_time_new_bad_parts in [
    ("missing", lambda: time.struct_time.__new__(), ("not enough arguments",)),
    ("class-only", lambda: time.struct_time.__new__(time.struct_time), ("missing required argument", "sequence")),
    ("extra", lambda: time.struct_time.__new__(time.struct_time, (1, 2, 3, 4, 5, 6, 7, 8, 9), {}, {}), ("at most 2 arguments", "3 given")),
    ("type", lambda: time.struct_time.__new__(time.struct_time, 42), ("constructor requires a sequence",)),
    ("unexpected", lambda: time.struct_time.__new__(time.struct_time, value=(1, 2, 3, 4, 5, 6, 7, 8, 9)), ("missing required argument", "sequence")),
    ("duplicate", lambda: time.struct_time.__new__(time.struct_time, (1, 2, 3, 4, 5, 6, 7, 8, 9), sequence=(1, 2, 3, 4, 5, 6, 7, 8, 9)), ("given by name", "sequence")),
    ("dict-extra-field", lambda: time.struct_time.__new__(time.struct_time, (1, 2, 3, 4, 5, 6, 7, 8, 9), {"x": 1}), ("duplicate or unexpected field name",)),
    ("kw-extra", lambda: time.struct_time.__new__(time.struct_time, sequence=(1, 2, 3, 4, 5, 6, 7, 8, 9), dict={}, value=1), ("at most 2 keyword arguments", "3 given")),
]:
    try:
        struct_time_new_bad_call()
    except TypeError as err:
        struct_time_new_bad_message = str(err)
        print("struct-time-new-diagnostic", struct_time_new_bad_name, all(part in struct_time_new_bad_message for part in struct_time_new_bad_parts))
dict_init_probe = {"old": 0}
dict.__init__(dict_init_probe, {"tm_zone": "DI"}, tm_gmtoff=654)
for dict_init_bad_name, dict_init_bad_call, dict_init_bad_parts in [
    ("missing", lambda: dict.__init__(), ("needs an argument",)),
    ("extra", lambda: dict.__init__(dict(), dict(), dict()), ("at most 1 argument", "got 2")),
    ("receiver", lambda: dict.__init__(list(), dict()), ("requires a 'dict' object", "list")),
    ("source-type", lambda: dict.__init__(dict(), 1), ("int", "not iterable")),
]:
    try:
        dict_init_bad_call()
    except TypeError as err:
        dict_init_bad_message = str(err)
        print("dict-init-diagnostic", dict_init_bad_name, all(part in dict_init_bad_message for part in dict_init_bad_parts))
StructTimeDictSubclass = type("StructTimeDictSubclass", (dict,), {})
constructed_dict_subclass_time = time.struct_time((2026, 8, 26, 1, 2, 3, 2, 238, -1), StructTimeDictSubclass({"tm_zone": "DS", "tm_gmtoff": 321}))
constructed_preserved_time = time.struct_time((2026, 8, 26, 1, 2, 3, 9, 999, -1))
constructed_string_field_time = time.struct_time((2026, 8, 26, 1, 2, 3, "weekday", 238, -1))
for time_strptime_bad_name, time_strptime_bad_call, time_strptime_bad_parts in [
    ("missing", lambda: time.strptime(), ("missing 1 required positional argument", "data_string")),
    ("extra", lambda: time.strptime("2026", "%Y", "x"), ("takes from 1 to 2 positional arguments", "3 were given")),
    ("keyword", lambda: time.strptime(string="2026", format="%Y"), ("takes no keyword arguments",)),
    ("text-type", lambda: time.strptime(1, "%Y"), ("argument 0 must be str", "int")),
    ("format-type", lambda: time.strptime("2026", 1), ("argument 1 must be str", "int")),
]:
    try:
        time_strptime_bad_call()
    except TypeError as err:
        time_strptime_bad_message = str(err)
        print("time-strptime-diagnostic", time_strptime_bad_name, all(part in time_strptime_bad_message for part in time_strptime_bad_parts))
parsed_time = time.strptime("2026-08-26", "%Y-%m-%d")
parsed_year_only = time.strptime("2026", "%Y")
parsed_month_day = time.strptime("08-26", "%m-%d")
parsed_clock_only = time.strptime("01:02:03", "%H:%M:%S")
parsed_yday = time.strptime("2026 239", "%Y %j")
parsed_yday_common_overflow = time.strptime("2026 366", "%Y %j")
parsed_yday_leap_last = time.strptime("2024 366", "%Y %j")
parsed_yday_overrides_date = time.strptime("2026 08 26 240", "%Y %m %d %j")
parsed_offset = time.strptime("+05:30", "%z")
parsed_offset_compact_seconds = time.strptime("+053045", "%z")
parsed_offset_fractional_seconds = time.strptime("+05:30:45.123456", "%z")
parsed_offset_full_day = time.strptime("+24:00", "%z")
parsed_offset_negative_full_day = time.strptime("-24:00:00", "%z")
parsed_offset_large = time.strptime("+99:59:59", "%z")
parsed_offset_zulu = time.strptime("Z", "%z")
parsed_zone = time.strptime("UTC", "%Z")
parsed_zone_lower_utc = time.strptime("utc", "%Z")
parsed_zone_lower_gmt = time.strptime("gmt", "%Z")
parsed_local_zone_standard = time.strptime(time.tzname[0], "%Z")
parsed_local_zone_daylight = time.strptime(time.tzname[1], "%Z")
parsed_short_year_low = time.strptime("68 01 02", "%y %m %d")
parsed_short_year_high = time.strptime("69 01 02", "%y %m %d")
parsed_midnight = time.strptime("12 AM", "%I %p")
parsed_noon = time.strptime("12 PM", "%I %p")
parsed_pm_hour = time.strptime("01 pm", "%I %p")
parsed_blank_24hour = time.strptime(" 9", "%k")
parsed_blank_12hour_pm = time.strptime(" 9 pm", "%l %P")
parsed_blank_12hour_midnight = time.strptime("12 AM", "%l %p")
parsed_fraction_short = time.strptime("2026-08-26 01:02:03.1", "%Y-%m-%d %H:%M:%S.%f")
parsed_fraction_long = time.strptime("2026-08-26 01:02:03.123456", "%Y-%m-%d %H:%M:%S.%f")
parsed_week_sunday = time.strptime("2026 35 3", "%Y %U %w")
parsed_week_monday = time.strptime("2026 34 3", "%Y %W %w")
parsed_week_iso_day = time.strptime("2026 35 7", "%Y %W %u")
parsed_week_zero_previous = time.strptime("2026 00 0", "%Y %U %w")
parsed_week_zero_current = time.strptime("2026 00 0", "%Y %W %w")
parsed_week_overflow = time.strptime("2026 53 1", "%Y %W %w")
parsed_iso_week = time.strptime("2026 35 3", "%G %V %u")
parsed_iso_week_name = time.strptime("Wednesday 2026 35", "%A %G %V")
parsed_iso_week_53 = time.strptime("2020 53 7", "%G %V %u")
parsed_locale_datetime = time.strptime("Wed Aug 26 01:02:03 2026", "%c")
parsed_locale_datetime_spaced_day = time.strptime("Wed Aug  6 01:02:03 2026", "%c")
parsed_locale_full_names = time.strptime("wednesday august 26 2026", "%A %B %d %Y")
parsed_locale_date = time.strptime("08/26/26", "%x")
parsed_locale_time = time.strptime("01:02:03", "%X")
parsed_locale_hour_minute = time.strptime("1:02", "%R")
parsed_locale_time_seconds = time.strptime("1:02:03", "%T")
parsed_locale_12hour_pm = time.strptime("01:02:03 PM", "%r")
parsed_locale_12hour_midnight = time.strptime("12:00:00 AM", "%r")
parsed_blank_hour = time.strptime(" 1", "%H")
parsed_blank_12hour = time.strptime(" 1", "%I")
parsed_blank_locale_time = time.strptime(" 1:02:03", "%X")
parsed_blank_locale_hour_minute = time.strptime(" 1:02", "%R")
parsed_blank_locale_time_seconds = time.strptime(" 1:02:03", "%T")
parsed_blank_locale_12hour = time.strptime(" 1:02:03 PM", "%r")
parsed_blank_locale_datetime = time.strptime("Wed Aug  6  1:02:03 2026", "%c")
parsed_space_day = time.strptime(" 6", "%e")
parsed_space_padded_day = time.strptime(" 7", "%d")
parsed_default_year_leap_day = time.strptime("02/29", "%m/%d")
parsed_whitespace_run = time.strptime("2026   08\t26", "%Y %m %d")
parsed_whitespace_format_run = time.strptime("2026 08 26", "%Y   %m\t%d")
parsed_whitespace_tab_run = time.strptime("2026\t08   26", "%Y %m %d")
parsed_locale_datetime_tab_day = time.strptime("Wed Aug\t6 01:02:03 2026", "%c")
print(constructed_time.tm_year, constructed_time[1], parsed_time.tm_year, parsed_time.tm_mon, parsed_time.tm_mday)
print("struct-time-keyword", constructed_keyword_time.tm_year, constructed_keyword_time.tm_zone is None, constructed_keyword_time.tm_gmtoff is None, constructed_keyword_dict_time.tm_zone, constructed_keyword_dict_time.tm_gmtoff)
print("dict-init-direct", "old" not in dict_init_probe, dict_init_probe["tm_zone"], dict_init_probe["tm_gmtoff"])
print("struct-time-dict-subclass", constructed_dict_subclass_time.tm_zone, constructed_dict_subclass_time.tm_gmtoff)
print(tuple(parsed_year_only), tuple(parsed_month_day), tuple(parsed_clock_only))
print(tuple(parsed_yday), parsed_yday.tm_mon, parsed_yday.tm_mday, parsed_yday.tm_yday)
print(tuple(parsed_yday_common_overflow), tuple(parsed_yday_leap_last))
print(tuple(parsed_yday_overrides_date), parsed_yday_overrides_date.tm_mon, parsed_yday_overrides_date.tm_mday, parsed_yday_overrides_date.tm_yday)
print(parsed_offset.tm_zone is None, parsed_offset.tm_gmtoff, parsed_zone.tm_zone, parsed_zone.tm_gmtoff is None, parsed_zone.tm_isdst)
print(parsed_zone_lower_utc.tm_zone, parsed_zone_lower_gmt.tm_zone, parsed_zone_lower_utc.tm_isdst, parsed_zone_lower_gmt.tm_isdst)
print(parsed_local_zone_standard.tm_zone == time.tzname[0], parsed_local_zone_standard.tm_isdst == 0, parsed_local_zone_standard.tm_gmtoff is None)
print(parsed_local_zone_daylight.tm_zone == time.tzname[1], parsed_local_zone_daylight.tm_isdst in (0, 1), parsed_local_zone_daylight.tm_gmtoff is None)
print(parsed_offset_compact_seconds.tm_gmtoff, parsed_offset_fractional_seconds.tm_gmtoff, parsed_offset_zulu.tm_gmtoff)
print(parsed_offset_full_day.tm_gmtoff, parsed_offset_negative_full_day.tm_gmtoff, parsed_offset_large.tm_gmtoff)
print(tuple(parsed_short_year_low), tuple(parsed_short_year_high))
print(parsed_midnight.tm_hour, parsed_noon.tm_hour, parsed_pm_hour.tm_hour, parsed_pm_hour.tm_wday)
print(parsed_blank_24hour.tm_hour, parsed_blank_12hour_pm.tm_hour, parsed_blank_12hour_midnight.tm_hour)
print(tuple(parsed_fraction_short), tuple(parsed_fraction_long))
print(tuple(parsed_week_sunday), tuple(parsed_week_monday), tuple(parsed_week_iso_day))
print(tuple(parsed_week_zero_previous), tuple(parsed_week_zero_current), tuple(parsed_week_overflow))
print(tuple(parsed_iso_week), tuple(parsed_iso_week_name), tuple(parsed_iso_week_53))
print(tuple(parsed_locale_datetime), tuple(parsed_locale_date), tuple(parsed_locale_time), parsed_space_day.tm_mday, parsed_space_padded_day.tm_mday)
print(tuple(parsed_locale_datetime_spaced_day), tuple(parsed_locale_full_names))
print(tuple(parsed_locale_hour_minute), tuple(parsed_locale_time_seconds), parsed_locale_12hour_pm.tm_hour, parsed_locale_12hour_midnight.tm_hour)
print(parsed_blank_hour.tm_hour, parsed_blank_12hour.tm_hour, parsed_blank_locale_time.tm_hour, parsed_blank_locale_hour_minute.tm_hour, parsed_blank_locale_time_seconds.tm_hour, parsed_blank_locale_12hour.tm_hour, parsed_blank_locale_datetime.tm_hour)
print(tuple(parsed_default_year_leap_day), parsed_default_year_leap_day.tm_mon, parsed_default_year_leap_day.tm_mday, parsed_default_year_leap_day.tm_yday)
print(tuple(parsed_whitespace_run)[:3], tuple(parsed_whitespace_format_run)[:3])
print(tuple(parsed_whitespace_tab_run)[:3], tuple(parsed_locale_datetime_tab_day)[:3])
print(constructed_time.n_fields, constructed_time.tm_zone is None, constructed_time.tm_gmtoff is None, constructed_zone_time.tm_zone, constructed_zone_time.tm_gmtoff)
print(constructed_dict_time.tm_zone, constructed_dict_time.tm_gmtoff, len(constructed_dict_time), constructed_dict_time.n_fields)
print(constructed_time.__repr__(), constructed_zone_time.__repr__() == constructed_dict_time.__repr__())
print("tm_wday='weekday'" in constructed_string_field_time.__repr__())
print(tuple(constructed_preserved_time)[6:9], constructed_preserved_time.tm_wday, constructed_preserved_time.tm_yday)
print(constructed_string_field_time.tm_wday, tuple(constructed_string_field_time)[6])
print(isinstance(constructed_time, tuple), constructed_time.count(2026), constructed_time.count(2), constructed_time.index(238), constructed_time.index(2, 6))
print(constructed_time.index(2026, False, True), constructed_time.index(8, True, 9))
for struct_time_method_bad_name, struct_time_method_bad_call, struct_time_method_bad_parts in [
    ("count-missing", lambda: constructed_time.count(), ("tuple.count()", "exactly one argument", "0 given")),
    ("count-extra", lambda: constructed_time.count(1, 2), ("tuple.count()", "exactly one argument", "2 given")),
    ("count-keyword", lambda: constructed_time.count(value=2026), ("tuple.count()", "takes no keyword arguments")),
    ("count-unbound-missing", lambda: time.struct_time.count(), ("unbound method tuple.count()", "needs an argument")),
    ("count-receiver", lambda: time.struct_time.count([], 1), ("descriptor 'count'", "tuple", "list")),
    ("index-missing", lambda: constructed_time.index(), ("index expected at least 1 argument", "got 0")),
    ("index-extra", lambda: constructed_time.index(1, 0, 1, 2), ("index expected at most 3 arguments", "got 4")),
    ("index-keyword", lambda: constructed_time.index(value=238), ("tuple.index()", "takes no keyword arguments")),
    ("index-unbound-missing", lambda: time.struct_time.index(), ("unbound method tuple.index()", "needs an argument")),
    ("index-receiver", lambda: time.struct_time.index([], 1), ("descriptor 'index'", "tuple", "list")),
    ("repr-extra", lambda: constructed_time.__repr__(1), ("expected 0 arguments", "got 1")),
    ("repr-keyword", lambda: constructed_time.__repr__(x=1), ("wrapper __repr__()", "takes no keyword arguments")),
    ("repr-unbound-missing", lambda: time.struct_time.__repr__(), ("descriptor '__repr__'", "needs an argument")),
    ("repr-receiver", lambda: time.struct_time.__repr__([]), ("descriptor '__repr__'", "time.struct_time", "list")),
]:
    try:
        struct_time_method_bad_call()
    except TypeError as err:
        struct_time_method_bad_message = str(err)
        print("struct-time-method-diagnostic", struct_time_method_bad_name, all(part in struct_time_method_bad_message for part in struct_time_method_bad_parts))
try:
    constructed_time.index("missing")
except ValueError as err:
    print("struct-time-index-missing", "not in tuple" in str(err))
for bad_struct_time_name, bad_struct_time_call, bad_struct_time_parts in [
    ("missing", lambda: time.struct_time(), ("missing required argument", "sequence")),
    ("extra-args", lambda: time.struct_time((2026, 8, 26, 1, 2, 3, 2, 238, -1), {}, {}), ("takes at most 2 arguments", "3 given")),
    ("sequence-type", lambda: time.struct_time(1), ("constructor requires a sequence",)),
    ("dict-type", lambda: time.struct_time((2026, 8, 26, 1, 2, 3, 2, 238, -1), 1), ("takes a dict as second arg",)),
    ("dict-extra-field", lambda: time.struct_time((2026, 8, 26, 1, 2, 3, 2, 238, -1), {"x": 1}), ("duplicate or unexpected field name",)),
    ("short-sequence", lambda: time.struct_time((2026, 8, 26)), ("at least 9-sequence", "3-sequence given")),
    ("long-sequence", lambda: time.struct_time((2026, 8, 26, 1, 2, 3, 2, 238, -1, "X", 123, 0)), ("at most 11-sequence", "12-sequence given")),
    ("sequence-keyword-missing", lambda: time.struct_time(dict={}), ("missing required argument", "sequence")),
    ("unexpected-keyword", lambda: time.struct_time((2026, 8, 26, 1, 2, 3, 2, 238, -1), other={}), ("unexpected keyword argument", "other")),
    ("duplicate-sequence-keyword", lambda: time.struct_time((2026, 8, 26, 1, 2, 3, 2, 238, -1), sequence=(2026, 8, 26, 1, 2, 3, 2, 238, -1)), ("given by name", "sequence", "position (1)")),
    ("extra-keywords", lambda: time.struct_time(sequence=(2026, 8, 26, 1, 2, 3, 2, 238, -1), dict={}, other=1), ("at most 2 keyword arguments", "3 given")),
    ("duplicate-dict-keyword", lambda: time.struct_time((2026, 8, 26, 1, 2, 3, 2, 238, -1), {}, dict={}), ("at most 2 arguments", "3 given")),
]:
    try:
        bad_struct_time_call()
    except TypeError as err:
        bad_struct_time_message = str(err)
        print("struct-time-diagnostic", bad_struct_time_name, all(part in bad_struct_time_message for part in bad_struct_time_parts))
for bad_struct_time_args in [
    ((2026, 8, 26, 1, 2, 3, 2, 238, -1, "X"), {"tm_zone": "Y"}),
    ((2026, 8, 26, 1, 2, 3, 2, 238, -1), {"unexpected": "Y"}),
    ((2026, 8, 26, 1, 2, 3, 2, 238, -1, "X", 123, 0),),
]:
    try:
        time.struct_time(*bad_struct_time_args)
    except TypeError as err:
        print("struct-time-extra", "field name" in str(err) or "at most 11-sequence" in str(err))
for bad_strptime_args in [
    ("2026x", "%Y"),
    ("2026 ", "%Y"),
    ("2026-08-26 01:02:03.", "%Y-%m-%d %H:%M:%S.%f"),
    ("z", "%z"),
    ("+05:3045", "%z"),
    ("+05:30:45.1234567", "%z"),
    ("Wed Aug 26 01:02:03", "%c"),
    ("Wed Aug6 01:02:03 2026", "%c"),
    ("WedAug 6 01:02:03 2026", "%c"),
    ("20260826", "%Y %m %d"),
    (" 0", "%e"),
    ("2026-02-31", "%Y-%m-%d"),
    ("2023-02-29", "%Y-%m-%d"),
    ("2026-04-31", "%Y-%m-%d"),
    ("Feb 31", "%b %d"),
    ("  7", "%d"),
    ("  1", "%H"),
    ("  1", "%I"),
    ("  1:02:03", "%X"),
    ("  1:02", "%R"),
    ("  1:02:03 PM", "%r"),
    ("13:02:03 PM", "%r"),
    ("01:02:03PM", "%r"),
]:
    try:
        time.strptime(*bad_strptime_args)
    except ValueError as err:
        print("strptime-trailing", "match format" in str(err) or "range" in str(err) or "out of range" in str(err))
for bad_strptime_iso_args, bad_strptime_iso_text in [
    (("2026 35 3", "%Y %V %u"), "incompatible with the year directive"),
    (("35 3", "%V %u"), "ISO week directive"),
    (("2026 35", "%G %V"), "ISO year directive"),
    (("2026 239", "%G %j"), "not compatible with ISO year"),
    (("2021 53 7", "%G %V %u"), "Invalid week: 53"),
    (("0000 01 1", "%G %V %u"), "year must be in 1..9999"),
]:
    try:
        time.strptime(*bad_strptime_iso_args)
    except ValueError as err:
        print("strptime-iso-error", bad_strptime_iso_text in str(err))
try:
    time.strptime("0000-01-01", "%Y-%m-%d")
except ValueError as err:
    print("strptime-year-range", "year must be in 1..9999" in str(err))
for bad_strptime_directive_args in [
    ("08/26/26", "%D"),
    ("2026-08-26", "%F"),
    ("x", "%Q"),
    ("x", "%"),
    ("2026", "%Y %"),
    ("2026", "%Y %Q"),
]:
    try:
        time.strptime(*bad_strptime_directive_args)
    except ValueError as err:
        print("strptime-bad-directive", "bad directive" in str(err) or "stray %" in str(err))
print(isinstance(time.timezone, int), isinstance(time.altzone, int), isinstance(time.daylight, int))
print(len(time.tzname) == 2, isinstance(time.tzname[0], str), isinstance(time.tzname[1], str), time.altzone <= time.timezone if time.daylight else time.altzone == time.timezone)
print("stdlib" in sysconfig.get_path_names(), "purelib" in sysconfig.get_paths(), sysconfig.get_python_version())
print(sysconfig.get_default_scheme() in sysconfig.get_scheme_names(), sysconfig.get_preferred_scheme("user") in sysconfig.get_scheme_names(), sysconfig.is_python_build())
# sysconfig preferred-scheme and expansion helpers.
preferred = sysconfig._get_preferred_schemes()
expanded_paths = sysconfig._expand_vars("nt", {"base": "BASE", "platbase": "PLAT"})
print(preferred["prefix"] in sysconfig.get_scheme_names(), preferred["home"] in sysconfig.get_scheme_names(), expanded_paths["purelib"].startswith("BASE"))
print(sysconfig.get_preferred_scheme("prefix") in sysconfig.get_scheme_names(), sysconfig.get_preferred_scheme("home"), sysconfig._get_sysconfigdata_name().startswith("_sysconfigdata"))
uname = platform.uname()
print(platform.python_implementation(), platform.python_version_tuple()[0], len(platform.python_compiler()) >= 0)
print(platform.system() == uname.system, platform.machine() == uname.machine, isinstance(platform.architecture()[0], str), isinstance(platform.libc_ver(), tuple))
# platform OS-version helper tuple shapes.
print(len(platform.win32_ver()), len(platform.mac_ver()), len(platform.java_ver()), platform.system_alias("SunOS", "5.10", "x")[0])
print(platform._sys_version()[0], isinstance(platform.freedesktop_os_release(), dict))
config_vars = sysconfig.get_config_vars()
print(sysconfig.get_makefile_filename().endswith("Makefile"), sysconfig.get_config_h_filename().endswith("pyconfig.h"))
print(sysconfig.expand_makefile_vars("$(py_version)-${SOABI}", config_vars) == sysconfig.get_config_var("py_version") + "-" + sysconfig.get_config_var("SOABI"))
print(opcode.opmap["LOAD_CONST"], opcode.opname[opcode.opmap["RESUME"]], opcode.HAVE_ARGUMENT, opcode.EXTENDED_ARG, opcode.cmp_op[2])
token_items = list(tokenize.tokenize(iter([b"a=1\n", b""]).__next__))
print(token_items[0].type, token_items[0].string == "utf-8", token_items[1].type, token_items[1].string, token_items[2].type, token_items[2].string, token_items[-1].type)
latin_tokens = list(tokenize.tokenize(iter([b"# coding: latin-1\n", b"name='caf\xe9'\n", b""]).__next__))
print(latin_tokens[0].string, latin_tokens[3].string, latin_tokens[5].type == tokenize.STRING)
bom_tokens = list(tokenize.tokenize(iter([b"\xef\xbb\xbfvalue=7\n", b""]).__next__))
print(bom_tokens[0].string, bom_tokens[1].string, bom_tokens[2].string)
comment_tokens = list(tokenize.tokenize(iter([b"x = '#'\n", b"# note\n", b"\n", b"y = 2 # tail\n", b""]).__next__))
comment_kinds = [item.type for item in comment_tokens if item.string in ("# note", "# tail", "\n")]
comment_text = [item.string for item in comment_tokens if item.type == tokenize.COMMENT]
print(comment_text, comment_kinds.count(tokenize.COMMENT), comment_kinds.count(tokenize.NL))
print(threading.__file__.endswith("threading.py"), os.__file__.endswith("os.py"))
print(winreg.HKEY_CURRENT_USER, winreg.KEY_READ, winreg.REG_SZ, winreg.CloseKey(winreg.HKEY_CURRENT_USER))
print(len(dis.findlinestarts(original.__code__)) > 0, len(dis.Bytecode(original)) > 0, len(dis.get_instructions(original.__code__)) > 0)
signature = inspect.signature(original)
print(list(signature.parameters.keys()), signature.parameters["a"].name, inspect.getmembers(wrapper, inspect.isroutine) == [])
bound_signature = signature.bind(4, 5)
print(bound_signature.arguments["a"], inspect.unwrap(wrapper) is original, inspect.getmodulename("sample.py"))
print(inspect.getdoc(original), inspect.getmro(OrderedValue)[0] is OrderedValue)
parsed_url = urllib.parse.urlparse("https://example.com/a/b;p?q=1#frag")
split_url = urllib.parse.urlsplit("https://example.com/a/b?q=1#frag")
print(parsed_url.scheme, parsed_url.netloc, parsed_url.path, parsed_url.params, parsed_url.query, parsed_url.fragment)
print(len(parsed_url), parsed_url[1], parsed_url.geturl())
print(split_url.scheme, split_url.path, len(split_url), split_url[2], split_url.geturl())
print(urllib.parse.urlunparse(parsed_url))
print(urllib.parse.urlunsplit(split_url))
print(urllib.parse.urljoin("https://e.com/a/b/c", "../d?q=1"))
print(urllib.parse.parse_qsl("a=1&b=two+words&a=3"))
print(urllib.parse.parse_qs("a=1&b=two+words&a=3")["a"])
print(urllib.parse.urlencode({"a": "two words", "b": 3}))
print(urllib.parse.quote("a b/c", "/"), urllib.parse.quote_plus("a b/c"))
print(urllib.parse.unquote("a%20b"), urllib.parse.unquote_plus("a+b"))
packed_struct = struct.pack("<hI2s?", -2, 513, b"xy", True)
print(struct.calcsize("<hI2s?"), len(packed_struct), packed_struct.hex())
print(struct.unpack("<hI2s?", packed_struct))
print(struct.unpack(">h", struct.pack(">h", 258))[0], struct.unpack("5p", struct.pack("5p", b"abcdef"))[0])
struct_buffer = bytearray(b"00000000")
print(struct.pack_into("<I", struct_buffer, 2, 0x11223344))
print(struct_buffer.hex(), struct.unpack_from("<I", struct_buffer, 2)[0])
print(list(struct.iter_unpack("<h", struct.pack("<hhh", 1, 2, 3))))
struct_obj = struct.Struct("<hI")
print(struct_obj.format, struct_obj.calcsize(), struct_obj.unpack(struct_obj.pack(-1, 7)))
try:
    struct.unpack("<I", b"x")
except struct.error as err:
    print(err.__class__.__name__)

# re: compiled patterns, match data, and common helpers.
m = re.search("([a-z]+)([0-9]+)", "id42")
compiled = re.compile("[a-z]+")
print(m.group(0), m.group(1), m.groups(), m.span(2))
print(compiled.match("abc").group(0), compiled.search("123abc").group(0), re.fullmatch("[0-9]+", "123") is not None)
print(re.findall("[0-9]+", "a1b22"), re.split(",", "a,b,c"), re.sub("[0-9]+", "#", "a12b3"))
print(re.ASCII, re.A, re.NOFLAG, re.VERBOSE, re.X, re.RegexFlag.__name__)
print(re.compile(br"[a-z]+", re.ASCII).match(b"abc").group(0))

# codecs: normalized lookup plus UTF-8 and hex encode/decode foundations.
codec_info = codecs.lookup("UTF-8")
print(codec_info.name, codecs.decode(codecs.encode("codec", "utf-8"), "utf_8"), codecs.decode(b"6869", "hex"))
print(codecs.lookup("idna").name, codecs.decode(codecs.encode("example.com", "idna"), "idna"))

# io: memory streams support common file-like read/write/seek/context helpers.
text_stream = io.StringIO("a\nb")
print(text_stream.readline().strip(), len(text_stream.readlines()), text_stream.seekable(), text_stream.closed())
with io.BytesIO(b"ab") as byte_stream:
    byte_stream.seek(2)
    byte_stream.write(b"c")
    print(byte_stream.getvalue(), byte_stream.readable(), byte_stream.writable())
print(byte_stream.closed())

signal_seen = []

def signal_handler(signum, frame):
    signal_seen.append(signum)

previous_handler = signal.signal(signal.SIGINT, signal_handler)
print(previous_handler == signal.SIG_DFL, signal.getsignal(signal.SIGINT) is signal_handler, signal.SIGINT in signal.valid_signals())
signal.raise_signal(signal.SIGINT)
print(signal_seen, signal.strsignal(signal.SIGTERM))
try:
    signal.default_int_handler(signal.SIGINT, None)
except KeyboardInterrupt:
    print("keyboard")

# json: CPython API names, formatting kwargs, hooks, and file-like dump/load.
json_compact = json.dumps({"b": 1, "a": [2, 3]}, sort_keys=True, separators=(",", ":"))
print(json_compact)

def json_hook(obj):
    obj["hooked"] = True
    return obj

def json_pairs_hook(pairs):
    return pairs[0][0] + str(pairs[0][1])

def parse_num(text):
    return "n:" + text

print(json.loads('{"x":1}', object_hook=json_hook)["hooked"])
print(json.loads('{"z":3}', object_pairs_hook=json_pairs_hook))
print(json.loads('{"n":42}', parse_int=parse_num)["n"])
json_stream = io.StringIO()
json.dump({"a": 1}, json_stream, indent=2)
print("\n" in json_stream.getvalue(), json.load(io.StringIO('{"k": 9}'))["k"])
print(json.JSONEncoder().encode([1, 2]), list(json.JSONEncoder().iterencode({"a": 1}))[0])

marshal_payload = {"n": [1, 2, (3, "x")], "b": b"hi", "none": None, "truth": True}
marshal_copy = marshal.loads(marshal.dumps(marshal_payload))
print(marshal_copy["n"][2][1], marshal_copy["b"] == b"hi", marshal_copy["none"] is None, marshal_copy["truth"])
marshal_stream = io.BytesIO()
marshal.dump([4, "stream"], marshal_stream)
marshal_stream.seek(0)
print(marshal.load(marshal_stream)[1], marshal.version)

pickle_payload = {"items": [1, "two"], "flag": False}
pickle_copy = pickle.loads(pickle.dumps(pickle_payload, 4))
print(pickle_copy["items"][1], pickle_copy["flag"], pickle.HIGHEST_PROTOCOL)
pickle_stream = io.BytesIO()
pickle.dump(("p", 3), pickle_stream)
pickle_stream.seek(0)
print(pickle.load(pickle_stream)[0])
pickle_stream2 = io.BytesIO()
pickle.Pickler(pickle_stream2).dump({"p": [1, 2]})
pickle_stream2.seek(0)
print(pickle.Unpickler(pickle_stream2).load()["p"][1])

xml = xmlrpc.client.dumps((7, "rpc"), methodname="demo.echo")
xml_params, xml_method = xmlrpc.client.loads(xml)
print(xml_method, xml_params[0], xml_params[1])
print(http.HTTPStatus.OK, http.client.responses[404], http.client.HTTP_PORT)

file_parts = __file__.replace("\\", "/").split("/")
core_fixture_dir = "/".join(file_parts[:-2] + ["core"])
compat_fixture_dir = "/".join(file_parts[:-1])

# os/os.path filesystem queries are routed through XLang3 VFS.
print(os.path.isfile(__file__), os.path.isdir(core_fixture_dir), os.path.exists(core_fixture_dir + "/missing.file") == False)
print(os.path.relpath(__file__, core_fixture_dir).endswith("standard_modules.py"), os.path.samefile(__file__, os.path.abspath(__file__)))
print(os.path.commonprefix(["alpha_one", "alpha_two"]), os.path.expandvars("$XLANG3_MISSING_VAR") == "$XLANG3_MISSING_VAR")
print(os.path.realpath("") == os.getcwd(), os.path.abspath("") == os.getcwd())
print(os.path.split("alpha/beta/gamma.txt"), os.path.commonpath(["alpha/beta/a.py", "alpha/beta/c.py"]))
print(os.path.normpath("alpha/./beta/../gamma"), os.path.basename("alpha/beta.txt"), os.path.dirname("alpha/beta.txt"))
mode = os.stat(__file__)[stat.ST_MODE]
print(stat.S_ISREG(mode), stat.S_ISDIR(mode), stat.S_IFMT(mode) == stat.S_IFREG, stat.S_IMODE(mode) >= 0)

# Common os filesystem operations stay behind the VFS.
os_dir = "xlang3_os_dir"
if os.path.exists(os_dir + "/renamed.txt"):
    os.remove(os_dir + "/renamed.txt")
if os.path.exists(os_dir + "/replaced.txt"):
    os.remove(os_dir + "/replaced.txt")
if os.path.isdir(os_dir):
    os.rmdir(os_dir)
os.mkdir(os_dir)
with open(os_dir + "/created.txt", "w") as f:
    f.write("abc")
print(os.path.lexists(os_dir), os.path.getsize(os_dir + "/created.txt"), os.access(os_dir + "/created.txt", os.F_OK))
os.rename(os_dir + "/created.txt", os_dir + "/renamed.txt")
with open(os_dir + "/replaced.txt", "w") as f:
    f.write("old")
os.replace(os_dir + "/renamed.txt", os_dir + "/replaced.txt")
fs_path_obj = pathlib.Path(os_dir + "/replaced.txt")
print(os.path.exists(os_dir + "/renamed.txt") == False, os.path.getsize(os_dir + "/replaced.txt"), os.fsdecode(os.fsencode(fs_path_obj)) == os.fspath(fs_path_obj))
os.remove(os_dir + "/replaced.txt")
os.rmdir(os_dir)
print(os.path.exists(os_dir) == False, isinstance(os.getcwdb(), bytes))

# os.scandir behaves as an iterator and context manager.
scan = os.scandir(compat_fixture_dir)
first_entry = next(scan)
print(isinstance(first_entry, os.DirEntry), first_entry.name in os.listdir(compat_fixture_dir), first_entry.path.endswith(first_entry.name))
scan.close()
try:
    next(scan)
except StopIteration:
    print("scandir-closed")

# DirEntry methods accept CPython keyword forms.
with os.scandir(compat_fixture_dir) as entries:
    found_standard = False
    for entry in entries:
        if entry.name == "standard_modules.py":
            found_standard = True
            print(entry.is_file(follow_symlinks=False), entry.is_dir(follow_symlinks=True), entry.is_symlink(), entry.inode() > 0, entry.stat(follow_symlinks=False).st_size > 0)
    print(found_standard)

# Path-like bytes inputs produce bytes names and paths.
byte_entries = os.listdir(bytes(compat_fixture_dir, "utf-8"))
byte_entry = next(os.scandir(bytes(compat_fixture_dir, "utf-8")))
print(isinstance(byte_entries[0], bytes), isinstance(byte_entry.name, bytes), isinstance(byte_entry.path, bytes))

path_obj = pathlib.Path("xlang3_pathlib_section.txt")
print(path_obj.name, path_obj.stem, path_obj.suffix, path_obj.suffixes)
print(path_obj.with_suffix(".bin").name, path_obj.with_name("renamed.txt").name, path_obj.parts[-1])
print(path_obj.write_text("path text"), path_obj.read_text())
print(path_obj.write_bytes(b"xy"), path_obj.read_bytes(), path_obj.exists(), path_obj.is_file(), path_obj.is_absolute())
path_dir = pathlib.Path("xlang3_pathlib_dir")
path_dir.mkdir(exist_ok=True)
nested_dir = path_dir / "sub" / "deep"
nested_dir.mkdir(parents=True, exist_ok=True)
root_note = path_dir / "root.txt"
deep_note = nested_dir / "note.txt"
root_note.write_text("root")
deep_note.write_text("deep")
print(path_dir.exists(), path_dir.is_dir(), deep_note.parent.name, deep_note.resolve().is_absolute())
print(sorted([item.name for item in path_dir.iterdir()]))
print(sorted([item.name for item in path_dir.glob("*.txt")]), sorted([item.name for item in path_dir.rglob("*.txt")]))
print(root_note.match("*.txt"), deep_note.match("xlang3_pathlib_dir/sub/deep/*.txt"), str(path_dir / "sub").endswith("sub"))
root_note.unlink()
(path_dir / "missing.txt").unlink(missing_ok=True)
print(root_note.exists() == False, list(path_dir.glob("*.txt")) == [])
glob_root = pathlib.Path("xlang3_glob_case")
os.makedirs("xlang3_glob_case/sub", exist_ok=True)
(glob_root / "a.py").write_text("a")
(glob_root / "b.txt").write_text("b")
(glob_root / "sub" / "c.py").write_text("c")
(glob_root / ".hidden.py").write_text("h")
print(glob.glob("xlang3_glob_case/*.py"))
print(glob.glob("xlang3_glob_case/**/*.py", True))
print(list(glob.iglob("xlang3_glob_case/*.txt")))
print(glob.glob("*.py", root_dir="xlang3_glob_case"))
print(glob.glob("*.py", root_dir="xlang3_glob_case", include_hidden=True))
hidden_iter = glob.iglob("*.py", root_dir="xlang3_glob_case", include_hidden=True)
print(next(hidden_iter), list(hidden_iter))
byte_glob = glob.glob(bytes("*.txt", "utf-8"), root_dir=bytes("xlang3_glob_case", "utf-8"))
print(isinstance(byte_glob[0], bytes), byte_glob)

found_functions = False
for module_info in pkgutil.iter_modules([core_fixture_dir]):
    if module_info[1] == "functions":
        found_functions = module_info[2] == False

resource = pkgutil.get_data("", core_fixture_dir + "/functions.py")
site.addsitedir(core_fixture_dir)
print(found_functions, len(resource) > 0, core_fixture_dir in sys.path, isinstance(site.PREFIXES, list))
import importlib.resources

print(pkgutil.resolve_name("functools:reduce") is functools.reduce, importlib.util.resolve_name(".client", "http"))
site.addsitedir(compat_fixture_dir)
import resource_pkg
print(importlib.resources.is_resource(resource_pkg, "data.txt"), importlib.resources.read_text(resource_pkg, "data.txt").strip())

# operator: generic runtime dispatch helpers and getter/caller factories.
import operator

values = [3, 4, 5]
operator.setitem(values, 1, 8)
print(operator.add(2, 5), operator.mul("ha", 2), operator.floordiv(17, 5), operator.mod(17, 5))
print(operator.eq(values[1], 8), operator.lt(2, 3), operator.contains(values, 5), operator.getitem(values, 1))
print(operator.itemgetter(0, 2)(values))

class OperatorInner:
    def __init__(self):
        self.name = "inner"

class OperatorBox:
    def __init__(self):
        self.inner = OperatorInner()

    def label(self, prefix):
        return prefix + self.inner.name

box = OperatorBox()
print(operator.attrgetter("inner.name")(box), operator.methodcaller("label", "box:")(box))
operator.delitem(values, 0)
print(values, operator.truth(values), operator.not_([]), operator.is_(box, box), operator.is_not(box, values))
print(operator.length_hint(values), operator.countOf([1, 2, 1], 1), operator.indexOf(["a", "b"], "b"))
print(operator.iadd([1], [2]), operator.iconcat("x", "y"), operator.iand(6, 3), operator.ior(4, 1), operator.ixor(6, 3))
print(operator.__getitem__(values, 0), operator.__add__(2, 4), operator.__lt__(1, 2), operator.__abs__(-7))
print(operator.call(original, 6, 7), operator.__contains__(values, 5), operator.__neg__(3), operator.__invert__(3))

# itertools: finite iterator helpers consume generic iterables correctly.
import itertools

def less_than_four(x):
    return x < 4

def is_even(x):
    return x % 2 == 0

class StandardIter:
    def __init__(self, values):
        self.values = values
        self.index = 0

    def __iter__(self):
        return self

    def __next__(self):
        if self.index >= len(self.values):
            raise StopIteration()
        value = self.values[self.index]
        self.index = self.index + 1
        return value

print(list(itertools.islice([0, 1, 2, 3, 4, 5], 1, 5, 2)))
print(list(itertools.takewhile(less_than_four, [1, 2, 5, 3])))
print(list(itertools.dropwhile(less_than_four, [1, 2, 5, 3])))
print(list(itertools.filterfalse(is_even, [1, 2, 3, 4])))
print(list(itertools.compress(["a", "b", "c"], [1, 0, 1])), list(itertools.repeat("x", 3)))
callable_iter_count = 0
def callable_iter_source():
    global callable_iter_count
    callable_iter_count = callable_iter_count + 1
    return callable_iter_count

chain_probe = itertools.chain([9], [10])
print(list(itertools.chain([1, 2], (3, 4))), chain_probe.__next__(), chain_probe.__next__(), list(iter(callable_iter_source, 3)), list(itertools.batched([1, 2, 3, 4, 5], 2)))
print(list(itertools.product([1, 2], ["a", "b"])))
print(list(itertools.combinations([1, 2, 3], 2)), list(itertools.combinations_with_replacement(["x", "y"], 2)))
print(list(itertools.permutations([1, 2, 3], 2)))
print(list(itertools.accumulate([1, 2, 3, 4])), list(itertools.starmap(original, [(1, 2), (3, 4)])))
print(list(itertools.zip_longest([1, 2], ["a"])))
print(list(itertools.islice(StandardIter([0, 1, 2, 3]), 1, 3)))
print(list(itertools.chain(StandardIter([1]), StandardIter([2]))), list(itertools.product(StandardIter([1, 2]), StandardIter(["x"]))))
print(list(itertools.combinations(StandardIter([1, 2, 3]), 2)), list(itertools.permutations(StandardIter([1, 2]), 2)))
print(list(itertools.accumulate(StandardIter([1, 2, 3]))), list(itertools.starmap(original, StandardIter([(5, 6)]))))
tee_left, tee_right = itertools.tee(StandardIter([4, 5, 6]))
print(list(itertools.pairwise([1, 2, 3, 4])), next(tee_left), list(tee_left), list(tee_right))

# collections: Counter, OrderedDict, ChainMap, and namedtuple foundations.
from collections import ChainMap, Counter, OrderedDict, deque, namedtuple

Pair = namedtuple("Pair", "left right")
pair = Pair._make(StandardIter([7, 8]))
print(pair.left, pair.right, Pair._fields)

counts = Counter("abbccc")
counts.update(StandardIter(["a", "d"]))
counts.subtract({"c": 1, "d": 2})
print(counts["a"], counts["b"], counts["c"], counts["d"], counts["z"])
print(counts.total(), counts.most_common(2))
print(list(counts.elements()))
dq = deque(StandardIter([1, 2]))
dq.extend(StandardIter([3]))
dq.extendleft(StandardIter([0]))
print(list(dq), dq[0], dq[-1], 2 in dq)

ordered = OrderedDict({"a": 1, "b": 2})
print(list(ordered.keys()), list(ordered.values()), list(ordered.items()))

chain = ChainMap({"a": 1}, {"a": 10, "b": 2})
chain["c"] = 3
child = chain.new_child({"a": 99})
print(chain["a"], chain["b"], chain["c"], chain.get("z", 7), "b" in chain, len(chain))
print(list(chain.keys()), list(chain.items()), child["a"], child["b"])
operator.setitem(chain, "d", 4)
print(operator.getitem(chain, "d"), operator.length_hint(chain), operator.contains(chain, "d"))

# queue: Queue variants keep distinct ordering, maxsize, and catchable exceptions.
import queue

fifo = queue.Queue(maxsize=2)
fifo.put("first")
fifo.put("second")
print(fifo.full(), fifo.qsize(), fifo.get(), fifo.get(), fifo.empty())
try:
    fifo.get_nowait()
except queue.Empty:
    print("empty")

lifo = queue.LifoQueue()
lifo.put(1)
lifo.put(2)
print(lifo.get(), lifo.get())

prio = queue.PriorityQueue()
prio.put((2, "b"))
prio.put((1, "a"))
print(prio.get(), prio.get())

limited = queue.Queue(1)
limited.put("x")
try:
    limited.put_nowait("y")
except queue.Full:
    print("full")
limited.get()
limited.task_done()
limited.join()
print(limited.empty())

kw_queue = queue.Queue(maxsize=2)
kw_queue.put("kw", block=False, timeout=None)
print(kw_queue.get(block=False, timeout=None))

shutdown_queue = queue.Queue()
shutdown_queue.put("before")
shutdown_queue.shutdown()
print(shutdown_queue.get())
try:
    shutdown_queue.get_nowait()
except queue.ShutDown:
    print("shutdown-empty")
try:
    shutdown_queue.put_nowait("after")
except queue.ShutDown:
    print("shutdown-put")

# subprocess: run/Popen foundations with captured text and catchable check failures.
completed = subprocess.run(["cmd", "/c", "echo xlang3-subprocess"], capture_output=True, text=True)
print(isinstance(completed, subprocess.CompletedProcess), completed.returncode, completed.stdout.strip())
raw_completed = subprocess.run(["cmd", "/c", "echo raw"], stdout=subprocess.PIPE)
print(raw_completed.returncode, len(raw_completed.stdout) > 0)
proc = subprocess.Popen(["cmd", "/c", "exit 0"])
print(proc.wait(), proc.poll())
try:
    subprocess.run(["cmd", "/c", "exit 7"], check=True, capture_output=True, text=True)
except subprocess.CalledProcessError as err:
    print(err.returncode, err.cmd[2], err.stdout == "")
shell_completed = subprocess.run("echo shell-ok", shell=True, capture_output=True, text=True)
print(shell_completed.stdout.strip())
input_completed = subprocess.run(["cmd", "/c", "more"], input="stdin-ok", stdout=subprocess.PIPE, text=True)
print(input_completed.stdout.strip())
merged_completed = subprocess.run(["cmd", "/c", "echo merged-error 1>&2"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
print(merged_completed.stdout.strip())
pipe_proc = subprocess.Popen(["cmd", "/c", "more"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
pipe_out, pipe_err = pipe_proc.communicate(b"pipe-ok")
print(pipe_proc.pid > 0, pipe_proc.returncode, isinstance(pipe_out, bytes), pipe_err is None, len(pipe_out) > 0)
with subprocess.Popen(["cmd", "/c", "exit 0"]) as context_proc:
    print(context_proc.pid > 0)
try:
    subprocess.run(["cmd", "/c", "ping -n 3 127.0.0.1 >nul"], timeout=0.01, shell=True)
except subprocess.TimeoutExpired as err:
    print(err.cmd[0], err.timeout > 0)
