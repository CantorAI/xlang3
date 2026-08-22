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

# .py source files, indentation-based blocks, comments.
total = 0

# Simple statements on separate lines and semicolon-separated simple statements.
a = 1
b = 2; c = 3

# Line continuation with backslash.
continued = 1 + \
    2

# Implicit line continuation across brackets.
implicit = [
    a,
    b,
    c,
]

# Compound statement simple suites on one line.
def one_line_func(): return 4
class OneLineClass: pass
if True: total = total + one_line_func()

# import name, import name as alias, import package.module.
import import_target
import import_target as import_alias
import pkg.module

# from module import name, from module import name as alias, from module import *.
from import_target import VALUE
from import_target import VALUE as VALUE_ALIAS
from import_target import *

# global.
def bump_global():
    global total
    total = total + 1

bump_global()

# nonlocal.
def make_counter():
    count = 0
    def inc():
        nonlocal count
        count = count + 1
        return count
    return inc

counter = make_counter()
total = total + counter()

# if, elif, else.
if total < 0:
    total = -1
elif total > 0:
    total = total + 1
else:
    total = 0

# while, break, continue.
i = 0
while i < 5:
    i = i + 1
    if i == 2:
        continue
    if i == 4:
        break
    total = total + i

# for, for tuple/list/starred target unpacking, for/else.
for x, y, *rest in [(1, 2, 3, 4)]:
    total = total + x + y + rest[0] + rest[1]
else:
    total = total + 1

# pass.
if False:
    pass

# return.
def returns_value():
    return 5

total = total + returns_value()

# raise expr, bare raise, raise ... from ..., try, except, except E as e, else, finally.
try:
    try:
        raise RuntimeError("inner")
    except RuntimeError as e:
        raise ValueError("outer") from e
except ValueError as caught:
    total = total + 1
else:
    total = -100
finally:
    total = total + 1

try:
    try:
        raise RuntimeError("again")
    except RuntimeError:
        raise
except RuntimeError:
    total = total + 1

# try/finally/else.
try:
    total = total + 1
except RuntimeError:
    total = -200
else:
    total = total + 1
finally:
    total = total + 1

# with expr as name, multiple context managers in one with, parenthesized multi-line with.
class Manager:
    def __enter__(self):
        return 2
    def __exit__(self, exc_type, exc_value, traceback):
        return False

with Manager() as first, Manager() as second:
    total = total + first + second

with (
    Manager() as third,
    Manager() as fourth,
):
    total = total + third + fourth

# del.
to_delete = 99
del to_delete

# assert.
assert total > 0

# match/case literal-expression, wildcard, soft keywords, structural pattern matching, OR, as, guard, class pattern.
class Point:
    __match_args__ = ("x", "y")
    def __init__(self, x, y):
        self.x = x
        self.y = y

match Point(3, [4, 5]):
    case Point(3, [matched, 5]) as whole if matched == 4:
        total = total + matched + whole.x
    case _:
        total = -300

match {"name": "xlang", "age": 3}:
    case {"name": name, "age": 3}:
        total = total + len(name)
    case _:
        total = -400

match [8, 2]:
    case [or_value, 1] | [or_value, 2]:
        total = total + or_value
    case _:
        total = -500

# Wildcard pattern inside OR.
match 99:
    case _ | 1:
        total = total + 1
    case _:
        total = -550

# Failed-pattern capture rollback.
failed_capture = "old"
match [1, 3]:
    case [failed_capture, 2]:
        total = -600
    case _:
        total = total + len(failed_capture)

# type parameter syntax accepted on def/class and runtime metadata basics.
def typed_identity[T](value):
    return value

class TypedBox[T]:
    pass

tp = typed_identity.__type_params__[0]
box_tp = TypedBox.__type_params__[0]

print("module-statement", total, import_target.VALUE, import_alias.VALUE, pkg.module.VALUE, VALUE, VALUE_ALIAS, STAR_VALUE)
print("type-params", tp.__name__, box_tp.__name__)
