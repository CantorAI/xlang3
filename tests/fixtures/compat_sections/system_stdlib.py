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

# CPython system stdlib source import probes. These must exercise the real
# Python files under C:/Python/Python314/Lib rather than public native facades.
import abc
import collections
import _collections_abc
import codecs
import copy
import copyreg
import dataclasses
import encodings
import enum
import inspect
import io
import json
import linecache
import logging
import queue
import pickle
import textwrap
import traceback
import types
import weakref
import _colorize


def source_lib_module(module):
    path = module.__file__.replace("\\", "/")
    return path.endswith("/Lib/" + module.__name__ + ".py")


def source_lib_package(module):
    path = module.__file__.replace("\\", "/")
    return path.endswith("/Lib/" + module.__name__ + "/__init__.py")


print(
    "system-stdlib-abc",
    source_lib_module(abc),
    abc.ABCMeta.__module__ == "abc",
    abc.ABC.__module__ == "abc",
    abc.abstractmethod.__module__ == "abc",
)
print(
    "system-stdlib-types",
    source_lib_module(types),
    types.WrapperDescriptorType is type(object.__init__),
    types.MethodWrapperType is type(object().__str__),
    types.MethodDescriptorType is type(str.join),
    types.ModuleType is type(types),
)
print(
    "system-stdlib-collections",
    source_lib_package(collections),
    source_lib_module(_collections_abc),
    collections.deque.__module__ == "collections",
    list(collections.deque([1, 2, 3])) == [1, 2, 3],
    isinstance(collections.UserDict({"a": 1}), collections.abc.MutableMapping),
)
q = queue.SimpleQueue()
q.put("first")
q.put("second")
print(
    "system-stdlib-queue",
    source_lib_module(queue),
    q.qsize(),
    q.get(),
    q.get_nowait(),
    q.empty(),
)


class WeakBox:
    pass


box = WeakBox()
ref = weakref.ref(box)
dictionary = weakref.WeakKeyDictionary()
dictionary[box] = "live"
weak_set = weakref.WeakSet()
weak_set.add(box)
print(
    "system-stdlib-weakref",
    source_lib_module(weakref),
    ref() is box,
    weakref.getweakrefcount(box) >= 1,
    list(dictionary.values()),
    [item is box for item in weak_set],
)
json_data = json.loads('{"name":"xlang3","items":[1,2,3],"enabled":true}')
copied = copy.copy({"a": [1, 2]})
payload = pickle.loads(pickle.dumps({"k": 7}))
print(
    "system-stdlib-json-pickle-copy",
    source_lib_package(json),
    source_lib_module(copy),
    source_lib_module(copyreg),
    source_lib_module(pickle),
    json_data["items"][1],
    copied["a"],
    payload["k"],
)
print(
    "system-stdlib-enum",
    source_lib_module(enum),
    enum.Enum.__module__ == "enum",
    enum.StrEnum.__module__ == "enum",
    [item.value for item in enum.FlagBoundary],
)
print(
    "system-stdlib-io-codecs",
    source_lib_module(io),
    source_lib_module(codecs),
    source_lib_package(encodings),
    codecs.lookup("utf-8").name,
    "x".encode("utf-8"),
    bytes([121]).decode("utf-8"),
)


# Inspect/traceback/logging pull several CPython source helpers through import.
@dataclasses.dataclass
class DataPoint:
    value: int = 1


print(
    "system-stdlib-source-helpers",
    source_lib_module(textwrap),
    source_lib_module(traceback),
    source_lib_module(inspect),
    source_lib_module(dataclasses),
    source_lib_package(logging),
    source_lib_module(linecache),
    source_lib_module(_colorize),
    type(inspect.signature(object)).__name__,
    DataPoint(7).value,
    logging.getLogger("xlang3").name,
    _colorize.can_colorize(),
)

# Source helpers need real mappingproxy/dict protocol behavior, not native
# stand-ins for the libraries themselves.
proxy_source = collections.OrderedDict([("first", 1), ("second", 2)])
proxy = types.MappingProxyType(proxy_source)
proxy_source["third"] = 3
print(
    "system-stdlib-mappingproxy",
    list(proxy),
    list(proxy.keys()),
    list(proxy.values()),
    list(proxy.items())[1],
    proxy["third"],
    hasattr(proxy, "__iter__"),
)


@dataclasses.dataclass(order=True)
class StdlibPoint:
    x: int
    y: int = 0


@dataclasses.dataclass(frozen=True, order=True)
class FrozenStdlibPoint:
    x: int
    y: int = 0


@dataclasses.dataclass
class FactoryStdlibBox:
    items: list = dataclasses.field(default_factory=list)


@dataclasses.dataclass(slots=True)
class SlotStdlibBox:
    value: int


sig = inspect.signature(lambda a, b=2, *, c=3: a + b + c)
bound = sig.bind(1, c=4)
dedented = textwrap.dedent(
    """
        alpha
          beta
    """
).strip()
try:
    raise ValueError("demo")
except ValueError as exc:
    formatted_exception = traceback.format_exception(exc)
stream = io.StringIO()
handler = logging.StreamHandler(stream)
handler.setFormatter(logging.Formatter("%(levelname)s:%(name)s:%(message)s"))
logger = logging.getLogger("xlang3.fixture")
logger.handlers = []
logger.setLevel(logging.INFO)
logger.addHandler(handler)
logger.propagate = False
logger.info("hello")
try:
    FrozenStdlibPoint(1).x = 9
except Exception as exc:
    frozen_error_name = type(exc).__name__
factory_left = FactoryStdlibBox()
factory_right = FactoryStdlibBox()
factory_left.items.append(7)
slot_box = SlotStdlibBox(5)
try:
    1 / 0
except Exception:
    logger.exception("failed %s", "division")
log_text = stream.getvalue()
print(
    "system-stdlib-source-helper-protocols",
    list(sig.parameters),
    bound.arguments["c"],
    dataclasses.asdict(StdlibPoint(2, 3)),
    dataclasses.astuple(dataclasses.replace(StdlibPoint(2, 3), y=9)),
    [field.name for field in dataclasses.fields(StdlibPoint)],
    dedented.splitlines(),
    any("ValueError: demo" in line for line in formatted_exception),
    linecache.getline(__file__, 1).startswith("# Copyright"),
    stream.getvalue().splitlines()[0],
)
print(
    "system-stdlib-dataclass-logging-deep",
    FrozenStdlibPoint(1) < FrozenStdlibPoint(2),
    frozen_error_name,
    factory_left.items,
    factory_right.items,
    slot_box.value,
    hasattr(slot_box, "__dict__"),
    "ERROR:xlang3.fixture:failed division" in log_text,
    "ZeroDivisionError" in log_text,
    "1 / 0" in log_text,
)
