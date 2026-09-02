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
import importlib.metadata
import inspect
import io
import json
import linecache
import logging
import os
import queue
import pickle
import re
import textwrap
import traceback
import typing
import types
import weakref
import _colorize


def source_lib_module(module):
    path = module.__file__.replace("\\", "/")
    return path.endswith("/Lib/" + module.__name__ + ".py")


def source_lib_package(module):
    path = module.__file__.replace("\\", "/")
    return path.endswith("/Lib/" + module.__name__.replace(".", "/") + "/__init__.py")


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
from typing import Match as TypingMatch

metadata_distributions = list(importlib.metadata.distributions())
print(
    "system-stdlib-importlib-metadata",
    source_lib_package(importlib.metadata),
    TypingMatch is typing.Match,
    type(metadata_distributions).__name__,
    len(metadata_distributions) >= 0,
)

# importlib.metadata should use CPython's pure Python package and XLang3's
# import/path/object primitives to discover real dist-info metadata.
metadata_root = "xlang3_meta_fixture"
metadata_info = metadata_root + "/demo_pkg-1.2.dist-info"
metadata_file = metadata_info + "/METADATA"
if os.path.exists(metadata_file):
    os.remove(metadata_file)
if os.path.isdir(metadata_info):
    os.rmdir(metadata_info)
if os.path.isdir(metadata_root):
    os.rmdir(metadata_root)
os.makedirs(metadata_info, exist_ok=True)
try:
    with open(metadata_file, "w", encoding="utf-8") as f:
        f.write("Metadata-Version: 2.1\nName: demo-pkg\nVersion: 1.2\n")
    discovered_metadata = list(importlib.metadata.distributions(name="demo-pkg", path=[metadata_root]))
    print(
        "system-stdlib-importlib-metadata-dist",
        len(discovered_metadata),
        discovered_metadata[0].metadata["Name"],
        discovered_metadata[0].version,
    )
finally:
    if os.path.exists(metadata_file):
        os.remove(metadata_file)
    if os.path.isdir(metadata_info):
        os.rmdir(metadata_info)
    if os.path.isdir(metadata_root):
        os.rmdir(metadata_root)
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

# re.py is CPython source, while _sre is the native dependency behind it.
named_match = re.search(r"(?P<word>[a-z]+)-(?P=word)", "abc-abc")
lookbehind_split = re.split(r"(?<!x),", "a,bx,c")
print(
    "system-stdlib-re-semantics",
    source_lib_package(re),
    named_match.group("word"),
    re.search(r"foo(?=bar)", "foobar").group(0),
    re.search(r"foo(?!bar)", "foobaz").group(0),
    re.search(r"(?<=foo)bar", "foobar").group(0),
    re.search(r"(?<!foo)bar", "xxbar").group(0),
    re.sub(r"([a-z]+)([0-9]+)", r"\2-\1", "id42"),
    [(match.group(0), match.start()) for match in re.finditer(r"\d+", "a12b3")],
    lookbehind_split,
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


# traceback: CPython 3.14 traceback.py consumes co_positions() columns.
def traceback_position_probe():
    value = (1 + 2) / 0


try:
    traceback_position_probe()
except Exception as exc:
    position_values = list(exc.__traceback__.tb_next.tb_frame.f_code.co_positions())
    position_columns = [item for item in position_values if item[2] is not None and item[3] is not None]
    formatted_text = "".join(traceback.format_exception(exc))
    print(
        "system-stdlib-traceback-positions",
        len(position_values) > 0,
        len(position_columns) > 0,
        "value = (1 + 2) / 0" in formatted_text,
        "ZeroDivisionError" in formatted_text,
        b"x".decode(encoding="utf-8", errors="strict"),
    )


# dataclasses: inheritance, InitVar/ClassVar, slots, and exec() globals interaction.
@dataclasses.dataclass
class StdlibDataclassBase:
    a: int


@dataclasses.dataclass
class StdlibDataclassChild(StdlibDataclassBase):
    b: int = 2
    temp: dataclasses.InitVar[int] = 3
    cv: typing.ClassVar[int] = 9
    xs: list = dataclasses.field(default_factory=list, kw_only=True)

    def __post_init__(self, temp):
        self.seen = temp


@dataclasses.dataclass(frozen=True)
class FrozenStdlibDataclassBase:
    a: int


def dataclass_error_name(source):
    try:
        exec(source, globals())
    except Exception as exc:
        return type(exc).__name__
    return "none"


@dataclasses.dataclass(slots=True)
class SlotStdlibDataclassBase:
    a: int


@dataclasses.dataclass(slots=True)
class SlotStdlibDataclassChild(SlotStdlibDataclassBase):
    b: int


child = StdlibDataclassChild(1, temp=8, xs=[4])
slot_child = SlotStdlibDataclassChild(1, 2)
print(
    "system-stdlib-dataclass-inheritance-slots",
    (child.a, child.b, child.seen, child.xs),
    [item.name for item in dataclasses.fields(StdlibDataclassChild)],
    dataclass_error_name("@dataclasses.dataclass\nclass BadFrozenChild(FrozenStdlibDataclassBase):\n    b:int=1"),
    dataclass_error_name("@dataclasses.dataclass(frozen=True)\nclass BadFrozenChild2(StdlibDataclassBase):\n    b:int=1"),
    (slot_child.a, slot_child.b, hasattr(slot_child, "__dict__")),
    (dataclasses.is_dataclass(StdlibDataclassChild), dataclasses.is_dataclass(child)),
)


# os fd APIs: CPython os.py should delegate to native nt/posix primitives.
fd_path = "xlang3_system_fd.tmp"
fd = os.open(fd_path, os.O_CREAT | os.O_TRUNC | os.O_RDWR | getattr(os, "O_BINARY", 0), 0o666)
try:
    written = os.write(fd, b"abcdef")
    end_pos = os.lseek(fd, 0, os.SEEK_END)
    start_pos = os.lseek(fd, 2, os.SEEK_SET)
    chunk = os.read(fd, 3)
    stat_size = os.fstat(fd).st_size
finally:
    os.close(fd)
    os.remove(fd_path)
pipe_read, pipe_write = os.pipe()
try:
    pipe_written = os.write(pipe_write, b"xy")
    os.close(pipe_write)
    pipe_write = None
    pipe_data = os.read(pipe_read, 2)
finally:
    if pipe_write is not None:
        os.close(pipe_write)
    os.close(pipe_read)
dup_path = "xlang3_system_fd_dup.tmp"
dup_base = os.open(dup_path, os.O_CREAT | os.O_TRUNC | os.O_RDWR | getattr(os, "O_BINARY", 0), 0o666)
dup_fd = None
dup_target = None
try:
    os.write(dup_base, b"abcdef")
    dup_fd = os.dup(dup_base)
    dup_inheritable_before = os.get_inheritable(dup_fd)
    os.set_inheritable(dup_fd, True)
    dup_inheritable_after = os.get_inheritable(dup_fd)
    dup_target = os.open(dup_path, os.O_RDONLY | getattr(os, "O_BINARY", 0), 0o666)
    os.dup2(dup_base, dup_target, False)
    dup2_inheritable = os.get_inheritable(dup_target)
    os.lseek(dup_target, 1, os.SEEK_SET)
    dup_chunk = os.read(dup_target, 2)
finally:
    if dup_fd is not None:
        os.close(dup_fd)
    if dup_target is not None:
        os.close(dup_target)
    os.close(dup_base)
    os.remove(dup_path)
print(
    "system-stdlib-os-fd",
    source_lib_module(os),
    written,
    end_pos,
    start_pos,
    chunk,
    stat_size,
    pipe_written,
    pipe_data,
    (dup_inheritable_before, dup_inheritable_after, dup2_inheritable),
    dup_chunk,
    os.isatty(1) in (True, False),
)


# os.environ: CPython os.py mapping writes sync to getenv; putenv itself does
# not mutate the Python mapping.
env_key = "XLANG3_SYSTEM_ENV_PROBE"
os.environ.pop(env_key, None)
env_start = (os.getenv(env_key), env_key in os.environ)
os.environ[env_key] = "one"
env_set = (os.getenv(env_key), os.environ.get(env_key))
os.putenv(env_key, "two")
env_putenv = (os.getenv(env_key), os.environ.get(env_key))
os.environ.pop(env_key)
env_pop = (os.getenv(env_key), env_key in os.environ)
os.putenv(env_key, "three")
env_external_putenv = (os.getenv(env_key), os.environ.get(env_key))
os.unsetenv(env_key)
env_copy = os.environ.copy()
env_dict = dict(os.environ)
env_update_dict = {}
env_update_dict.update(os.environ)
print(
    "system-stdlib-os-environ",
    env_start,
    env_set,
    env_putenv,
    env_pop,
    env_external_putenv,
    os.getenv(env_key),
    (type(env_copy).__name__, len(env_copy) > 0, len(env_dict) > 0, len(env_update_dict) > 0),
)
