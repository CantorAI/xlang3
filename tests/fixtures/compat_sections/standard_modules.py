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

parser = argparse.ArgumentParser()
parser.add_argument("-n", "--num", type=int, default=1)
parser.add_argument("--verbose", action="store_true")
parser.add_argument("name")
parsed = parser.parse_args(["--num", "7", "--verbose", "bob"])
print(parsed.num, parsed.verbose, parsed.name)

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

# getpass/locale/sysconfig/opcode/winreg: common inspection helpers and constants.
import getpass
import locale
import opcode
import sysconfig
import winreg

print(len(getpass.getuser()) > 0, len(locale.getencoding()) > 0, locale.localeconv()["decimal_point"])
print("stdlib" in sysconfig.get_path_names(), "purelib" in sysconfig.get_paths(), sysconfig.get_python_version())
print(opcode.opmap["LOAD_CONST"], opcode.opname[opcode.opmap["RESUME"]], opcode.HAVE_ARGUMENT)
print(winreg.HKEY_CURRENT_USER, winreg.KEY_READ, winreg.REG_SZ, winreg.CloseKey(winreg.HKEY_CURRENT_USER))

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

# collections: Counter dict-backed counting helpers.
from collections import Counter

counts = Counter("abbccc")
counts.update(["a", "d"])
counts.subtract({"c": 1, "d": 2})
print(counts["a"], counts["b"], counts["c"], counts["d"], counts["z"])
print(counts.total(), counts.most_common(2))
print(list(counts.elements()))
