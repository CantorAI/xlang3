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
import code

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

def combine(a, b):
    return a * 10 + b

def cmp_num(a, b):
    return a - b

Key = functools.cmp_to_key(cmp_num)
print(functools.reduce(combine, [1, 2, 3]), functools.reduce(combine, [2, 3], 1))
print(Key(1) < Key(2), Key(2) > Key(1), Key(2) == Key(2), Key(3) != Key(2))
compiled_command = code.compile_command("answer = 42")
print(compiled_command.co_filename, compiled_command.co_name, code.compile_command("if True:") is None)

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

# getpass/locale/sysconfig/opcode/dis/winreg: common inspection helpers and constants.
import dis
import getpass
import http
import http.client
import io
import locale
import marshal
import opcode
import pickle
import pkgutil
import signal
import site
import subprocess
import sys
import sysconfig
import winreg
import xmlrpc.client

print(len(getpass.getuser()) > 0, len(locale.getencoding()) > 0, locale.localeconv()["decimal_point"])
print("stdlib" in sysconfig.get_path_names(), "purelib" in sysconfig.get_paths(), sysconfig.get_python_version())
print(opcode.opmap["LOAD_CONST"], opcode.opname[opcode.opmap["RESUME"]], opcode.HAVE_ARGUMENT)
print(winreg.HKEY_CURRENT_USER, winreg.KEY_READ, winreg.REG_SZ, winreg.CloseKey(winreg.HKEY_CURRENT_USER))
print(len(dis.findlinestarts(original.__code__)) > 0, len(dis.Bytecode(original)) > 0, len(dis.get_instructions(original.__code__)) > 0)

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

marshal_payload = {"n": [1, 2, (3, "x")], "b": b"hi", "none": None, "truth": True}
marshal_copy = marshal.loads(marshal.dumps(marshal_payload))
print(marshal_copy["n"][2][1], marshal_copy["b"] == b"hi", marshal_copy["none"] is None, marshal_copy["truth"])
marshal_stream = io.BytesIO()
marshal.dump([4, "stream"], marshal_stream)
marshal_stream.seek(0)
print(marshal.load(marshal_stream)[1], marshal.version)

pickle_payload = {"items": [1, "two"], "flag": False}
pickle_copy = pickle.loads(pickle.dumps(pickle_payload))
print(pickle_copy["items"][1], pickle_copy["flag"], pickle.HIGHEST_PROTOCOL)
pickle_stream = io.BytesIO()
pickle.dump(("p", 3), pickle_stream)
pickle_stream.seek(0)
print(pickle.load(pickle_stream)[0])

xml = xmlrpc.client.dumps((7, "rpc"), methodname="demo.echo")
xml_params, xml_method = xmlrpc.client.loads(xml)
print(xml_method, xml_params[0], xml_params[1])
print(http.HTTPStatus.OK, http.client.responses[404], http.client.HTTP_PORT)

file_parts = __file__.replace("\\", "/").split("/")
core_fixture_dir = "/".join(file_parts[:-2] + ["core"])
found_functions = False
for module_info in pkgutil.iter_modules([core_fixture_dir]):
    if module_info[1] == "functions":
        found_functions = module_info[2] == False

resource = pkgutil.get_data("", core_fixture_dir + "/functions.py")
site.addsitedir(core_fixture_dir)
print(found_functions, len(resource) > 0, core_fixture_dir in sys.path, isinstance(site.PREFIXES, list))

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

print(list(itertools.islice([0, 1, 2, 3, 4, 5], 1, 5, 2)))
print(list(itertools.takewhile(less_than_four, [1, 2, 5, 3])))
print(list(itertools.dropwhile(less_than_four, [1, 2, 5, 3])))
print(list(itertools.filterfalse(is_even, [1, 2, 3, 4])))
print(list(itertools.compress(["a", "b", "c"], [1, 0, 1])), list(itertools.repeat("x", 3)))
print(list(itertools.chain([1, 2], (3, 4))), list(itertools.batched([1, 2, 3, 4, 5], 2)))
print(list(itertools.product([1, 2], ["a", "b"])))
print(list(itertools.combinations([1, 2, 3], 2)), list(itertools.combinations_with_replacement(["x", "y"], 2)))
print(list(itertools.permutations([1, 2, 3], 2)))
print(list(itertools.accumulate([1, 2, 3, 4])), list(itertools.starmap(original, [(1, 2), (3, 4)])))
print(list(itertools.zip_longest([1, 2], ["a"])))

# collections: Counter, OrderedDict, and ChainMap foundations.
from collections import ChainMap, Counter, OrderedDict

counts = Counter("abbccc")
counts.update(["a", "d"])
counts.subtract({"c": 1, "d": 2})
print(counts["a"], counts["b"], counts["c"], counts["d"], counts["z"])
print(counts.total(), counts.most_common(2))
print(list(counts.elements()))

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
