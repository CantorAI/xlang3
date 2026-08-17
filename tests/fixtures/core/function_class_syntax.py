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

def defaults(a, b=5):
    return a + b

print(defaults(2), defaults(2, 8))

def pos_only(a, /, b):
    return a * b

print(pos_only(3, b=4))

def keyword_only(a, *, b=7):
    return a + b

print(keyword_only(5), keyword_only(5, b=9))

def varargs(a, *items):
    total = a
    for item in items:
        total = total + item
    return total

print(varargs(1, 2, 3))
more = (4, 5)
print(varargs(1, *more))

print(*("a", "b"), sep="-", end="!")
print()

def kwargs(a, **named):
    return a + named["x"] + named["y"]

values = {"x": 10, "y": 20}
print(kwargs(1, **values))
print(kwargs(1, x=2, y=3))

def combo(a, b=2, *rest, c=3, **named):
    total = a + b + c
    for item in rest:
        total = total + item
    return total + named["z"]

print(combo(1, 4, 5, 6, c=7, z=8))

def deco(fn):
    print("decorate")
    return fn

@deco
def decorated(x):
    return x + 1

print(decorated(9))

def native_identity(fn):
    return fn

try:
    import _builtins
    native_identity = _builtins._identity
except ImportError:
    pass

@native_identity
def native_decorated(x):
    return x + 2

print(native_decorated(9))

class Base:
    def value(self):
        return 30

@deco
class Child(Base):
    pass

c = Child()
print(c.value())

inc = lambda x: x + 1
print(inc(40))

def annotated[T](x: "int") -> "int":
    return x

print(annotated(44))
print(annotated.__annotations__["x"], annotated.__annotations__["return"])
print(defaults.__annotations__)

def gen_pair():
    yield 1
    yield 2

g = gen_pair()
print(next(g))
print(next(g))

def gen_range(n):
    i = 0
    while i < n:
        yield i
        i = i + 1

total = 0
for item in gen_range(4):
    total = total + item
print(total)

def gen_delegate():
    yield 8
    yield from [9, 10]

delegated = 0
for item in gen_delegate():
    delegated = delegated + item
print(delegated)

events = []

def gen_side_effects():
    events.append(1)
    yield 10
    events.append(2)
    yield 20

side = gen_side_effects()
print(events)
print(next(side), events)
print(next(side), events)
