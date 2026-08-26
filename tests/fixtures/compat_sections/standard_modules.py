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

# argparse: common parser shape with option aliases, typed values, flags, and positional args.
import argparse
import ast

parser = argparse.ArgumentParser()
parser.add_argument("-n", "--num", type=int, default=1)
parser.add_argument("--verbose", action="store_true")
parser.add_argument("name")
parsed = parser.parse_args(["--num", "7", "--verbose", "bob"])
print(parsed.num, parsed.verbose, parsed.name)

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

class NativeABC(metaclass=abc.ABCMeta):
    pass

class Concrete:
    pass

token_before = abc.get_cache_token()
print(NativeABC.register(Concrete) is Concrete, abc.get_cache_token() > token_before)
print(issubclass(Concrete, NativeABC), isinstance(Concrete(), NativeABC))
print(len(_abc._get_dump(NativeABC)[0]) >= 1, _abc._abc_subclasscheck(NativeABC, Concrete), _abc._abc_instancecheck(NativeABC, Concrete()))
_abc._reset_registry(NativeABC)
print(issubclass(Concrete, NativeABC), _abc._abc_subclasscheck(NativeABC, Concrete))

@abc.abstractmethod
def abstract_fn():
    pass

print(abstract_fn.__isabstractmethod__)

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
print(locale.delocalize("1,234.5"), locale.localize("1234.5"), locale.atoi("1,234"), locale.atof("1,234.5"))
print(locale.strcoll("a", "b") < 0, isinstance(locale.strxfrm("abc"), str), locale.CHAR_MAX)
old_recursion_limit = sys.getrecursionlimit()
sys.setrecursionlimit(old_recursion_limit + 1)
print(sys.getdefaultencoding(), sys.getfilesystemencoding(), sys.getfilesystemencodeerrors(), sys.getrecursionlimit() == old_recursion_limit + 1)
sys.setrecursionlimit(old_recursion_limit)
print(sys.intern("abc") == "abc", sys.getsizeof("abc") > 0, isinstance(sys.meta_path, list), isinstance(sys.path_hooks, list), isinstance(sys.path_importer_cache, dict))
clock_info = time.get_clock_info("monotonic")
print(clock_info.monotonic, clock_info.adjustable, clock_info.resolution > 0, isinstance(clock_info.implementation, str))
print(time.process_time() >= 0, time.process_time_ns() >= 0, time.thread_time() >= 0, time.thread_time_ns() >= 0)
epoch_utc = time.gmtime(0)
print(isinstance(epoch_utc, time.struct_time), epoch_utc.tm_year, epoch_utc.tm_mon, epoch_utc.tm_mday, time.strftime("%Y", epoch_utc))
print(time.mktime(time.localtime(0)) == 0.0, isinstance(time.tzname, tuple), isinstance(time.ctime(0), str))
print("stdlib" in sysconfig.get_path_names(), "purelib" in sysconfig.get_paths(), sysconfig.get_python_version())
print(sysconfig.get_default_scheme() in sysconfig.get_scheme_names(), sysconfig.get_preferred_scheme("user") in sysconfig.get_scheme_names(), sysconfig.is_python_build())
print(opcode.opmap["LOAD_CONST"], opcode.opname[opcode.opmap["RESUME"]], opcode.HAVE_ARGUMENT, opcode.EXTENDED_ARG, opcode.cmp_op[2])
token_items = list(tokenize.tokenize(iter([b"a=1\n", b""]).__next__))
print(token_items[0].type, token_items[0].string == "utf-8", token_items[1].type, token_items[1].string, token_items[2].type, token_items[2].string, token_items[-1].type)
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
mode = os.stat(__file__)[stat.ST_MODE]
print(stat.S_ISREG(mode), stat.S_ISDIR(mode), (mode & stat.S_IFMT) == stat.S_IFREG)

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
print(dq.to_list())

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
