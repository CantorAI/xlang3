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
import array
import collections
import _collections_abc
import codecs
import copy
import copyreg
import dataclasses
import encodings
import enum
import gc
import importlib.metadata
import importlib.machinery
import importlib.resources
import inspect
import io
import json
import linecache
import logging
import os
import pkgutil
import queue
import pickle
import re
import subprocess
import sys
import threading
import textwrap
import traceback
import unicodedata
from contextlib import contextmanager

empty_arrays = [array.array(typecode) for typecode in "bBhuwHiIlLfd"]
assert all(len(value) == 0 and re.fullmatch(b"", value) is not None for value in empty_arrays)
typed_bytes = array.array("B")
typed_bytes.append(65)
typed_bytes.frombytes(b"BC")
assert len(typed_bytes) == 3
assert typed_bytes[1] == 66
assert typed_bytes.tobytes() == b"ABC"
assert re.fullmatch(b"ABC", typed_bytes) is not None

assert float.__getformat__("double") == "IEEE, little-endian"
assert float.__getformat__("float") == "IEEE, little-endian"
assert "%d" % (2**128) == "340282366920938463463374607431768211456"
assert re.match(r".{,3}", "abcd").span() == (0, 3)
assert re.match(
    r"(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)(l)\119",
    "abcdefghijklk9",
).group(11) == "k"
assert bytes("ÿ", "latin-1") == b"\xff"
try:
    bytes("ÿ", "ascii")
except UnicodeEncodeError:
    pass
else:
    raise AssertionError("ASCII bytes encoding accepted a non-ASCII code point")
assert unicodedata.lookup("less-than sign") == "<"
assert re.match(r"\N{SNAKE}", "🐍").group() == "🐍"
assert re.match(r"^\d$", "๘").group() == "๘"
assert re.match(r"^\d$", "０").group() == "０"
assert re.match(r"^\d$", "Ⅵ") is None
assert re.search(r"^\Aabc\z$", "abc", re.MULTILINE).group() == "abc"
assert re.search(r"^\Aabc\z$", "\nabc\n", re.MULTILINE) is None
assert re.search(r"\b(ьюя)\b", "ьюя").group(1) == "ьюя"
assert re.search(r"\b(ьюя)\b", "ьюя", re.ASCII) is None
assert len(re.findall(r"\b", "a")) == 2
class StdlibIntSubclass(int):
    pass
assert 2 * StdlibIntSubclass(3) == 6

class StdlibAbstractProbe(metaclass=abc.ABCMeta):
    @abc.abstractmethod
    def run(self):
        pass

assert inspect.isabstract(StdlibAbstractProbe)
assert not inspect.isabstract(object)
assert not isinstance(map(str, []), str)
assert isinstance(map(str, []), object)
assert "__path__" not in dir(linecache)
assert "__path__" not in vars(linecache)
assert "a\nb".splitlines(keepends=True) == ["a\n", "b"]
assert "banana".count("a", 2, 5) == 1

@contextmanager
def stdlib_throw_context():
    yield

try:
    with stdlib_throw_context():
        raise ValueError("context-throw")
except ValueError as stdlib_context_error:
    assert str(stdlib_context_error) == "context-throw"
else:
    raise AssertionError("generator context manager suppressed ValueError")
import typing
import types
import weakref

gc_initial_state = gc.isenabled()
gc_thresholds = gc.get_threshold()
gc.disable()
assert not gc.isenabled()
gc.enable()
assert gc.isenabled()
gc.set_threshold(701, 11, 12)
assert gc.get_threshold() == (701, 11, 12)
gc.set_threshold(*gc_thresholds)
if not gc_initial_state:
    gc.disable()
assert gc.collect() == 0
assert ord(chr(0xD800)) == 0xD800
print("system-stdlib-native-runtime", gc.__name__ == "gc", gc.isenabled() == gc_initial_state, gc.get_threshold() == gc_thresholds, ord(chr(0xD800)))
import warnings

assert getattr(importlib.machinery, "__warningregistry__", None) is None
assert importlib.machinery.ModuleSpec is not None
import_error = ImportError("missing", name="demo.module", path="demo.py")
assert import_error.args == ("missing",)
assert import_error.name == "demo.module"
assert import_error.path == "demo.py"
module_spec = importlib.machinery.ModuleSpec(
    name="demo.module",
    loader=None,
    origin="demo.py",
    loader_state={"ready": True},
    is_package=True,
)
assert module_spec.name == "demo.module"
assert module_spec.loader is None
assert module_spec.origin == "demo.py"
assert module_spec.loader_state == {"ready": True}
assert module_spec.submodule_search_locations == []
assert importlib.machinery.BuiltinImporter.get_code("sys") is None
import runpy
import select
import site
import socket
import zipfile
import _colorize

try:
    socket.SocketType.fixture_attribute = 1
except TypeError as immutable_socket_error:
    assert "immutable" in str(immutable_socket_error)
else:
    raise AssertionError("_socket.socket must be immutable")


def source_lib_module(module, source_name=None):
    path = module.__file__.replace("\\", "/")
    name = module.__name__ if source_name is None else source_name
    return path.endswith("/Lib/" + name + ".py")


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
    source_lib_module(_collections_abc, "_collections_abc"),
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
proxy_box = WeakBox()
proxy_box.value = 7
proxy_ref = weakref.proxy(proxy_box)
assert proxy_ref.value == 7
proxy_box = None
gc.collect()
try:
    proxy_ref.value
except ReferenceError:
    pass
else:
    raise AssertionError("weakref proxy retained its target")
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

# Dictionary entries and instance attributes must use separate storage. Metadata
# discovery relies on defaultdict.values() excluding default_factory and _frozen.
class MetadataDict(dict):
    pass

metadata_dict = MetadataDict()
metadata_dict.label = "attribute"
metadata_dict["label"] = "entry"
assert metadata_dict.label == "attribute"
assert metadata_dict["label"] == "entry"
assert metadata_dict.__dict__ == {"label": "attribute"}
assert list(metadata_dict.values()) == ["entry"]
metadata_dict.__dict__["extra"] = 42
assert metadata_dict.extra == 42
assert "extra" not in metadata_dict
metadata_defaults = collections.defaultdict(list)
assert list(metadata_defaults.values()) == []
metadata_defaults.note = "attribute"
metadata_defaults["items"].append(7)
assert list(metadata_defaults.values()) == [[7]]
assert metadata_defaults.default_factory is list
assert metadata_defaults.note == "attribute"

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

# importlib/pkgutil/runpy/site should stay CPython-source-backed while using
# XLang3's import loader, VFS, descriptors, and module metadata underneath.
fixture_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(fixture_dir), "core"))
runpy_module_ns = runpy.run_module("runpy_support")
sys.path.insert(0, fixture_dir)
import resource_pkg

resources_root = importlib.resources.files(resource_pkg)
resources_item = resources_root.joinpath("data.txt")
pkgutil_resource = pkgutil.get_data("resource_pkg", "data.txt")
site_pth_root = "xlang3_site_pth_probe"
site_pth_extra_name = "extra"
site_pth_extra = os.path.join(site_pth_root, site_pth_extra_name)
os.makedirs(site_pth_extra, exist_ok=True)
try:
    with open(os.path.join(site_pth_root, "probe.pth"), "w", encoding="utf-8") as f:
        f.write(site_pth_extra_name + "\n")
        f.write("import sys; sys.xlang3_pth_marker = 42\n")
    if hasattr(sys, "xlang3_pth_marker"):
        del sys.xlang3_pth_marker
    site.addsitedir(site_pth_root)
    site_path_snapshot = [path.replace("\\", "/") for path in sys.path]
    site_pth_state = (
        any(path.endswith("/xlang3_site_pth_probe") or path == site_pth_root for path in site_path_snapshot),
        any(path.endswith("/xlang3_site_pth_probe/extra") or path == site_pth_extra for path in site_path_snapshot),
        getattr(sys, "xlang3_pth_marker", None),
    )
finally:
    if hasattr(sys, "xlang3_pth_marker"):
        del sys.xlang3_pth_marker
    site_pth_file = os.path.join(site_pth_root, "probe.pth")
    if os.path.exists(site_pth_file):
        os.remove(site_pth_file)
    for path in (site_pth_extra, site_pth_root):
        if os.path.isdir(path):
            os.rmdir(path)
print(
    "system-stdlib-site-runpy-importlib-resources",
    source_lib_module(site),
    source_lib_module(runpy),
    source_lib_module(pkgutil),
    source_lib_package(importlib.resources),
    runpy_module_ns["result"],
    pkgutil_resource.decode("utf-8").strip(),
    getattr(resources_root, "name"),
    getattr(resources_root, "parts")[-1],
    resources_item.is_file(),
    resources_item.read_text().strip(),
    site_pth_state,
)


class StdlibGetattrDescriptor:
    @property
    def value(self):
        return "descriptor-value"

    @property
    def boom(self):
        raise ValueError("descriptor-error")


descriptor_probe = StdlibGetattrDescriptor()
try:
    hasattr(descriptor_probe, "boom")
except Exception as exc:
    descriptor_hasattr_error = type(exc).__name__ + ":" + str(exc)
print(
    "system-stdlib-getattr-descriptor",
    getattr(descriptor_probe, "value"),
    getattr(descriptor_probe, "missing", "fallback"),
    hasattr(descriptor_probe, "value"),
    hasattr(descriptor_probe, "missing"),
    descriptor_hasattr_error,
)

# Namespace and zip package resources should be discovered by CPython's
# importlib.resources readers through XLang3 loader/resource-reader hooks.
namespace_root_a = "xlang3_ns_a"
namespace_root_b = "xlang3_ns_b"
namespace_pkg_a = namespace_root_a + "/ns_pkg"
namespace_pkg_b = namespace_root_b + "/ns_pkg"
namespace_file_a = namespace_pkg_a + "/a.txt"
namespace_file_b = namespace_pkg_b + "/b.txt"
for path in (namespace_file_a, namespace_file_b):
    if os.path.exists(path):
        os.remove(path)
for path in (namespace_pkg_a, namespace_pkg_b, namespace_root_a, namespace_root_b):
    if os.path.isdir(path):
        os.rmdir(path)
os.makedirs(namespace_pkg_a)
os.makedirs(namespace_pkg_b)
os.makedirs(namespace_pkg_b + "/__pycache__")
try:
    with open(namespace_file_a, "w", encoding="utf-8") as f:
        f.write("A")
    with open(namespace_file_b, "w", encoding="utf-8") as f:
        f.write("B")
    sys.path.insert(0, namespace_root_a)
    sys.path.insert(0, namespace_root_b)
    import ns_pkg

    namespace_files = importlib.resources.files(ns_pkg)
    print(
        "system-stdlib-namespace-resources",
        type(namespace_files).__name__,
        sorted([p.name for p in namespace_files.iterdir()]),
        namespace_files.joinpath("a.txt").read_text(),
        namespace_files.joinpath("b.txt").read_text(),
    )
finally:
    for path in (namespace_file_a, namespace_file_b):
        if os.path.exists(path):
            os.remove(path)
    for path in (namespace_pkg_b + "/__pycache__", namespace_pkg_a, namespace_pkg_b, namespace_root_a, namespace_root_b):
        if os.path.isdir(path):
            os.rmdir(path)

zip_resource_path = "xlang3_zip_resource_probe.zip"
if os.path.exists(zip_resource_path):
    os.remove(zip_resource_path)
try:
    with zipfile.ZipFile(zip_resource_path, "w") as zf:
        zf.writestr("zip_pkg/__init__.py", "VALUE = 123\n")
        zf.writestr("zip_pkg/data.txt", "ZIPDATA")
    sys.path.insert(0, zip_resource_path)
    import zip_pkg

    zip_files = importlib.resources.files(zip_pkg)
    print(
        "system-stdlib-zip-resources",
        zip_pkg.__loader__.__class__.__name__,
        zip_pkg.__spec__.loader is zip_pkg.__loader__,
        type(zip_files).__name__,
        pkgutil.get_data("zip_pkg", "data.txt"),
        zip_files.joinpath("data.txt").read_text(),
    )
finally:
    zip_files = None
    zip_pkg = None
    sys.modules.pop("zip_pkg", None)
    sys.path_importer_cache.pop(zip_resource_path, None)
    if zip_resource_path in sys.path:
        sys.path.remove(zip_resource_path)
    gc.collect()
    if os.path.exists(zip_resource_path):
        os.remove(zip_resource_path)
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
assert re.search(r"(?<!-):(.*?)(?<!-):", "a:bc-:de:f").group(1) == "bc-:de"
assert re.search(r"(?<!\\):(.*?)(?<!\\):", r"a:bc\:de:f").group(1) == r"bc\:de"
assert re.search(r"(?<!\?)'(.*?)(?<!\?)'", "a'bc?'de'f").group(1) == "bc?'de"
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
    re.findall(r"a.*c", "a\nbc", re.S),
    re.findall(r"^a", "x\na\na", re.M),
    re.findall(r"a+", "AaA", re.I),
)

assert re.findall(r"^a", "x\na\na", re.M) == ["a", "a"]
assert re.findall(r"(?i:a)b", "Ab aB AB") == ["Ab"]
assert re.findall(r"(?i:a(?-i:b))", "ab AB Ab") == ["ab", "Ab"]
assert re.match(r"\w", "à") is not None
assert re.match(r"\w", "à", re.ASCII) is None
assert re.match("À", "à", re.IGNORECASE) is not None
assert re.match("À", "à", re.ASCII | re.IGNORECASE) is None
assert re.match("K", "K", re.IGNORECASE) is not None
assert re.match(r"\u212a", "k", re.IGNORECASE) is not None
assert re.match("S", "ſ", re.IGNORECASE) is not None
assert re.match(r"\u017f", "s", re.IGNORECASE) is not None
assert re.match(r"\u0412", "ᲀ", re.IGNORECASE) is not None
assert re.match(r"\u1c80", "в", re.IGNORECASE) is not None
assert re.match(r"\ufb05", "ﬆ", re.IGNORECASE) is not None
assert re.match(r"[19\u212a]", "K", re.IGNORECASE) is not None
assert re.match(r"[\u0411-\u0413]", "ᲀ", re.IGNORECASE) is not None
assert re.match(r"[9-A]", "_", re.IGNORECASE) is None
assert re.match(r"[N-\uffff]", "A", re.ASCII | re.IGNORECASE) is not None
assert "K".lower() == "k"
assert "ſ".upper() == "S"
assert "ᲀ".upper() == "В"
assert "ﬅ".upper() == "ST" and "ﬆ".upper() == "ST"
assert re.match(r"(?-i:a)b", "Ab", re.IGNORECASE) is None
assert re.match(r"(?-i:a)b", "aB", re.IGNORECASE) is not None
assert re.match(r"\w(?a:\W)\w", "ààà") is not None
assert re.match(r"\W(?u:\w)\W", "ààà", re.ASCII) is not None
lookbehind_width_subject = "x" * 512
assert re.search(r"(?<=((.{8}){8}){8})", lookbehind_width_subject).span() == (512, 512)
assert re.search(r"(?<!((.{8}){8}){8})", lookbehind_width_subject).span() == (0, 0)
assert re.match(br"\w", b"\xe0") is None
assert re.fullmatch(r"(?is)a.b", "A\nb") is not None
assert re.fullmatch(" a(?x: b) c", " ab c") is not None
assert re.fullmatch(" a(?-x: b) c", "a bc", re.X) is not None
assert re.match(r"(?P<left>x)(y)", "xy").expand(r"\g<left>-\2") == "x-y"
assert re.split(r"\b", "Words") == ["", "Words", ""]
assert re.split(r"(?<=:)", ":a:b::c") == [":", "a:", "b:", ":", "c"]
assert re.split(r"\b|:+", "a::bc") == ["", "a", "", "", "bc", ""]
assert re.split(r"(?<!\w)(?=\w)|:+", "a::bc") == ["", "a", "", "bc"]
assert re.sub(r"\b|:+", "-", "a::bc") == "-a---bc-"
assert re.findall(r"\b|:+", "a::bc") == ["", "", "::", "", ""]
assert [match.span() for match in re.finditer(r"\b|\w+", "a::bc")] == [
    (0, 0), (0, 1), (1, 1), (3, 3), (3, 5), (5, 5)
]
assert re.fullmatch(br"a|ab", bytearray(b"ab")).span() == (0, 2)
assert re.fullmatch(br"a|ab", memoryview(b"ab")).span() == (0, 2)
live_regex_buffer = bytearray(b"abcdefgh")
live_regex_match = re.search(br"[a-h]+", live_regex_buffer)
live_regex_buffer[:] = b"xyz"
assert live_regex_match.group() == b"xyz"
locked_regex_buffer = bytearray(b"x")
locked_regex_iterator = re.finditer(br"a", locked_regex_buffer)
try:
    locked_regex_buffer.extend(b"x" * 400)
except BufferError:
    pass
else:
    raise AssertionError("finditer must export a mutable subject buffer")
assert list(locked_regex_iterator) == []
locked_regex_buffer.extend(b"x" * 400)
class RegexString(str):
    pass
class RegexBytes(bytes):
    pass
assert re.fullmatch(r"a|ab", RegexString("ab")).span() == (0, 2)
assert re.fullmatch(br"a|ab", RegexBytes(b"ab")).span() == (0, 2)
assert re.fullmatch(r"a]", "a]") is not None
assert re.fullmatch(r"w(?# first)xy(?# second)z", "wxyz") is not None
assert re.fullmatch(r"\a[\b]\f\n\r\t\v", "\a\b\f\n\r\t\v") is not None
assert re.fullmatch(r"(abc)\1", "abcabc").group(1) == "abc"
assert re.fullmatch(r"(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)\11", "abcdefghijkk").group(11) == "k"
assert re.fullmatch(r"(a)a(?<=\1)c", "aac") is not None
assert re.fullmatch(r"(a)a(?<!\1)c", "aac") is None
assert re.match(r"(?:(a)|(x))b(?<=(?(2)x|b))c", "abc") is not None
assert re.match(r"(?:(a)|(x))b(?<=(?(1)c|x))c", "abc") is None
captured_negative_lookbehind = re.match(r"^([ab]*?)(?<!(a))c", "abc")
assert captured_negative_lookbehind.groups() == ("ab", None)
assert captured_negative_lookbehind.span(2) == (-1, -1)
named_unicode_escape = "\N{LATIN CAPITAL LETTER A WITH DIAERESIS}"
assert named_unicode_escape == unicodedata.lookup("LATIN CAPITAL LETTER A WITH DIAERESIS")
assert re.fullmatch(r"(?u)\b.\b", named_unicode_escape).group() == named_unicode_escape
assert re.search(r"(?ms).*?x\s*\z(.*)", "xx\nx\n").group(1) == ""
assert re.match(r"(?:(a)|(x))b(?=(?(1)c|x))c", "abc") is not None
assert re.match(r"(a)b(?=(?(2)x|c))(c)", "abc") is not None
assert re.match(r"^(\()?([^()]+)(?(1)\))$", "(a)").groups() == ("(", "a")
assert re.match(r"^(\()?([^()]+)(?(1)\))$", "a").groups() == (None, "a")
assert re.match(r"^(?:(a)|c)((?(1)b|d))$", "cd").groups() == (None, "d")
assert re.match(r"(?P<g1>a)(?P<g2>b)?((?(g2)c|d))", "ad").groups() == ("a", None, "d")
assert re.match(r"a(?>bc|b)c", "abc") is None
assert re.match(r"a(?>bc|b)c", "abcc") is not None
assert re.match(r"e*+e", "eeee") is None
assert re.match(r"e{2,4}+a", "eeea").group() == "eeea"
assert re.findall(r"(?:ab)++", "ababc") == ["abab"]
assert re.match(r"((x)|y|z)*+", "xyz").groups() == ("z", "x")
assert re.match(r"(?:(?:a|bc)*?(xx)??z)*", "axxzbcz").groups() == ("xx",)
assert re.match(r"^((x|y)*)*", "xyyzy").groups() == ("", "y")
assert re.match(r"((a))", "a").lastindex == 1
assert re.match(r"(a)(b)?b", "ab").lastindex == 1
assert re.match(r"(?P<a>a)(?P<b>b)?b", "ab").lastgroup == "a"
for anchored_miss in (re.compile(r"\Ay"), re.compile(r"^y")):
    assert anchored_miss.search("xxxxx") is None
    assert anchored_miss.split("xxxxx") == ["xxxxx"]
    assert anchored_miss.findall("xxxxx") == []
    assert list(anchored_miss.finditer("xxxxx")) == []
    assert anchored_miss.sub("", "xxxxx") == "xxxxx"
assert re.fullmatch(r"\111", "I") is not None
for invalid_regex_subject in (5, type):
    try:
        re.search("x*", invalid_regex_subject)
    except TypeError as regex_subject_error:
        assert "got '" in str(regex_subject_error)
    else:
        raise AssertionError("invalid regex subjects must raise TypeError")

def increment_replacement(match):
    return str(int(match.group()) + 1)

assert re.sub(r"\d+", increment_replacement, "a1b22") == "a2b23"
assert re.subn(r"\d+", "#", "a1b22") == ("a#b#", 2)
assert re.sub(r"(?P<letter>[a-z])", r"\g<letter>\g<letter>", "ab") == "aabb"
assert re.sub("x", r"\000\a\b\f\v", "x") == "\0\a\b\f\v"
assert re.sub("x", r"\1111", "x") == "I1"
assert re.sub("$", "#", "a\n") == "a#\n#"
assert re.match(r"(a)(b)", "ab").group(2, 1) == ("b", "a")
assert re.match(r"(a)(b)?", "a").groups("") == ("a", "")
class RegexGroupIndex:
    def __index__(self):
        return 2
assert re.match(r"(a)(b)", "ab").group(RegexGroupIndex()) == "b"
assert [match.span() for match in re.compile(r"a").finditer(string="baac", pos=2, endpos=3)] == [(2, 3)]
keyword_pattern = re.compile(r"(ab)")
assert repr(re.compile("random pattern", re.I | re.S)) == "re.compile('random pattern', re.IGNORECASE|re.DOTALL)"
re.purge()
equal_pattern = re.compile(r"(ab)")
assert equal_pattern == keyword_pattern and hash(equal_pattern) == hash(keyword_pattern)
assert equal_pattern != re.compile(br"(ab)")
assert pickle.loads(pickle.dumps(equal_pattern)) == equal_pattern
assert copy.copy(keyword_pattern) is keyword_pattern
assert copy.deepcopy(keyword_pattern) is keyword_pattern
copy_match = keyword_pattern.match("ab")
assert copy.copy(copy_match) is copy_match and copy.deepcopy(copy_match) is copy_match
assert copy_match.regs == ((0, 2), (0, 2))
assert repr(copy_match) == "<_sre.SRE_Match object; span=(0, 2), match='ab'>"
named_pattern = re.compile(r"(?P<first>a)")
format_match = re.match(r"(?P<first>a)(?P<second>b)?", "a")
assert "first={first} second={second}".format_map(format_match) == "first=a second=None"

class FormatDefaults(dict):
    def __missing__(self, key):
        return "<" + key + ">"

assert "{present}:{absent}".format_map(FormatDefaults(present="yes")) == "yes:<absent>"
try:
    named_pattern.groupindex["first"] = 2
except TypeError:
    pass
else:
    raise AssertionError("Pattern.groupindex must be read-only")
assert keyword_pattern.match(string="abracadabra", pos=7, endpos=10).span() == (7, 9)
assert keyword_pattern.fullmatch(string="abracadabra", pos=7, endpos=9).span() == (7, 9)
assert keyword_pattern.search(string="abracadabra", pos=3, endpos=10).span() == (7, 9)
assert keyword_pattern.findall(string="abracadabra", pos=3, endpos=10) == ["ab"]
assert keyword_pattern.split(string="abracadabra", maxsplit=1) == ["", "ab", "racadabra"]
assert keyword_pattern.scanner(string="abracadabra", pos=3, endpos=10).search().span() == (7, 9)
try:
    keyword_pattern.match("ab").group(99)
except IndexError as missing_group_error:
    assert "no such group" in str(missing_group_error)
else:
    raise AssertionError("missing match groups must raise IndexError")
try:
    keyword_pattern.match("ab")[(0, 1)]
except IndexError as invalid_group_error:
    assert "no such group" in str(invalid_group_error)
else:
    raise AssertionError("non-integer match group keys must raise IndexError")

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


try:
    proxy["missing"]
except KeyError:
    pass
else:
    raise AssertionError("mappingproxy subscription misses must raise KeyError")

assert "name_1".isidentifier()
assert "µ".isidentifier()
assert "𝔘𝔫𝔦𝔠𝔬𝔡𝔢".isidentifier()
for invalid_identifier in ("©", "㊀", "¹", "१"):
    assert not invalid_identifier.isidentifier()

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


# threading.py stays CPython source-backed; _thread owns native synchronization
# and thread-local storage.
class StdlibLocalWithInit(threading.local):
    def __init__(self, value=1):
        self.value = value
        self.initialized = getattr(self, "initialized", 0) + 1


thread_local = StdlibLocalWithInit(5)
thread_seen = []


def thread_local_worker():
    thread_seen.append((hasattr(thread_local, "value"), thread_local.value, getattr(thread_local, "initialized", None)))
    thread_local.value = 9
    thread_seen.append((thread_local.value, thread_local.initialized))


thread = threading.Thread(target=thread_local_worker)
thread.start()
thread.join()
print(
    "system-stdlib-thread-local-subclass",
    source_lib_module(threading),
    thread_seen,
    thread_local.value,
    thread_local.initialized,
)

# Constructor keywords must be replayed when each thread initializes a
# threading.local subclass for the first time.
local_keyword_events = []


class KeywordLocal(threading.local):
    def __init__(self, value, *, enabled=False):
        self.value = value
        self.enabled = enabled
        local_keyword_events.append((value, enabled))


keyword_local = KeywordLocal(12, enabled=True)
keyword_thread_values = []


def read_keyword_local():
    keyword_thread_values.append((keyword_local.value, keyword_local.enabled))


keyword_thread = threading.Thread(target=read_keyword_local)
keyword_thread.start()
keyword_thread.join()
assert keyword_thread_values == [(12, True)]
assert local_keyword_events == [(12, True), (12, True)]
assert (keyword_local.value, keyword_local.enabled) == (12, True)

# _thread.exit raises SystemExit, which threading suppresses while still
# completing the thread lifecycle normally.
thread_exit_events = []


def exit_worker():
    import _thread

    thread_exit_events.append("before")
    _thread.exit()
    thread_exit_events.append("after")


exit_thread = threading.Thread(target=exit_worker)
exit_thread.start()
exit_thread.join()
assert thread_exit_events == ["before"]
assert not exit_thread.is_alive()

# CPython socket.py delegates datagram I/O to the native _socket methods.
udp_left = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
udp_right = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
try:
    udp_left.bind(("127.0.0.1", 0))
    udp_right.bind(("127.0.0.1", 0))
    assert udp_left.getsockopt(socket.SOL_SOCKET, socket.SO_TYPE) == socket.SOCK_DGRAM
    assert len(udp_left.getsockopt(socket.SOL_SOCKET, socket.SO_TYPE, 4)) == 4
    assert udp_left.sendto(b"udp", udp_right.getsockname()) == 3
    udp_readable, udp_writable, udp_exceptional = select.select([udp_right], [udp_left], [udp_left], 1.0)
    udp_payload, udp_address = udp_right.recvfrom(16)
    assert udp_payload == b"udp"
    assert udp_address[0] == "127.0.0.1"
    assert udp_readable == [udp_right]
    assert udp_writable == [udp_left]
    assert udp_exceptional == []
finally:
    udp_left.close()
    udp_right.close()

# reload() passes a target to _find_spec, reuses the module object, and asks
# the source loader to execute the updated file. FileFinder path hooks also
# drive pkgutil.iter_modules over ordinary directories.
reload_root = "xlang3_importlib_reload_probe"
reload_file = os.path.join(reload_root, "reload_target.py")
warning_file = os.path.join(reload_root, "warning_target.py")
os.makedirs(reload_root, exist_ok=True)
sys.path.insert(0, reload_root)
try:
    with open(reload_file, "w", encoding="utf-8") as stream:
        stream.write("VALUE = 1\n")
    importlib.invalidate_caches()
    reload_target = importlib.import_module("reload_target")
    with open(reload_file, "w", encoding="utf-8") as stream:
        stream.write("VALUE = 222\n")
    importlib.invalidate_caches()
    assert importlib.reload(reload_target) is reload_target
    assert reload_target.VALUE == 222
    assert [item.name for item in pkgutil.iter_modules([reload_root])] == ["reload_target"]
    with open(warning_file, "w", encoding="utf-8") as stream:
        stream.write("import warnings\nwarnings.warn('nested-frame', UserWarning, stacklevel=3)\n")
    importlib.invalidate_caches()
    with warnings.catch_warnings(record=True) as caught_warnings:
        warnings.simplefilter("always")
        importlib.import_module("warning_target")
    assert len(caught_warnings) == 1
    assert os.path.normcase(caught_warnings[0].filename) == os.path.normcase(__file__), (caught_warnings[0].filename, __file__)
finally:
    sys.path.remove(reload_root)
    sys.modules.pop("reload_target", None)
    sys.modules.pop("warning_target", None)
    if os.path.exists(reload_file):
        os.remove(reload_file)
    if os.path.exists(warning_file):
        os.remove(warning_file)
    if os.path.isdir(reload_root):
        os.rmdir(reload_root)


# subprocess.py stays CPython source-backed and delegates process work to
# native _winapi/os primitives.
subprocess_cwd = "xlang3_subprocess_cwd_probe"
os.makedirs(subprocess_cwd, exist_ok=True)
try:
    child_result = subprocess.run([sys.executable, "-c", "print('child-xlang')"], capture_output=True, text=True)
    stdin_process = subprocess.Popen(
        [sys.executable, "-c", "import sys; data=sys.stdin.read(); print(data.upper())"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    stdin_stdout, stdin_stderr = stdin_process.communicate("abc")
    check_call_result = subprocess.check_call([sys.executable, "-c", "import sys; sys.exit(0)"])
    called_error_state = None
    try:
        subprocess.run([sys.executable, "-c", "import sys; sys.exit(3)"], check=True)
    except subprocess.CalledProcessError as called_error:
        called_error_state = (called_error.returncode, called_error.cmd[0].endswith("xlang3.exe") or called_error.cmd[0].endswith("python.exe"))
    cwd_result = subprocess.run(["cmd", "/c", "cd"], cwd=subprocess_cwd, capture_output=True, text=True)
    devnull_result = subprocess.run(["cmd", "/c", "echo hidden"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        subprocess.run(["cmd", "/c", "ping -n 3 127.0.0.1 >nul"], shell=True, timeout=0.01)
    except subprocess.TimeoutExpired as timeout_error:
        timeout_state = (timeout_error.cmd is not None, timeout_error.timeout == 0.01)
finally:
    os.rmdir(subprocess_cwd)
print(
    "system-stdlib-subprocess-deep",
    source_lib_module(subprocess),
    child_result.returncode,
    child_result.stdout.strip(),
    stdin_process.returncode,
    stdin_stdout.strip(),
    stdin_stderr.strip(),
    check_call_result,
    called_error_state,
    cwd_result.returncode,
    cwd_result.stdout.strip().replace("\\", "/").endswith(subprocess_cwd),
    devnull_result.returncode,
    timeout_state,
)
