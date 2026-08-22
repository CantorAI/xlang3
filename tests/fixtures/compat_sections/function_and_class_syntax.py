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

# def, positional, default, positional-only, keyword-only, varargs, kwargs.
def signature(a, /, b=2, *items, c=3, **named):
    total = a + b + c + named["z"]
    for item in items:
        total = total + item
    return total

print(signature(1, 4, 5, 6, c=7, z=8))

# Function annotations and type parameters.
def annotated[T](x: "int") -> "int":
    return x

print(annotated(11), annotated.__annotations__["x"], annotated.__annotations__["return"])
print(annotated.__type_params__[0].__name__)

# Function decorators, including native callables.
def marker(fn):
    def wrapped(x):
        return fn(x) + 1
    return wrapped

native_identity = marker
try:
    import _builtins
    native_identity = _builtins._identity
except ImportError:
    pass

@marker
def decorated(x):
    return x * 2

@native_identity
def native_decorated(x):
    return x + 5

print(decorated(6), native_decorated(6))

# Nested functions and closures.
def make_counter(start):
    value = start
    def next_value(step=1):
        nonlocal value
        value = value + step
        return value
    return next_value

counter = make_counter(10)
print(counter(), counter(4))

# Classes, inheritance, multiple bases, class decorators, and class type parameters.
def class_marker(cls):
    cls.marked = "yes"
    return cls

class Left:
    name = "left"
    def left(self):
        return 20

class Right:
    def right(self):
        return 3

@class_marker
class Child[T](Left, Right):
    kind = "child"
    def total(self):
        return self.left() + self.right()

child = Child()
print(child.total(), Child.name, Child.kind, Child.marked, Child.__type_params__[0].__name__)

# Metaclass keyword syntax and evaluated keyword expression.
meta_seen = []

def choose_meta():
    meta_seen.append("eval")
    return type

class WithMeta(metaclass=choose_meta()):
    value = 99

print(WithMeta.value, meta_seen)

# Lambda expressions.
inc = lambda x: x + 1
print(inc(40))

# Generators: yield, send, close, and yield from.
def responder():
    received = yield 1
    yield received + 2

g = responder()
print(next(g))
print(g.send(5))
print(g.close())

def delegated():
    yield 7
    yield from [8, 9]

print(list(delegated()))

# Generator throw method surface.
def one():
    yield 1

thrown = one()
print(next(thrown))
try:
    thrown.throw(ValueError, "boom")
except ValueError:
    print("throw")

# async def, await, async for, async with.
import asyncio

async def add(a, b):
    return a + b

class AsyncCounter:
    def __init__(self, limit):
        self.limit = limit
        self.i = 0
    def __aiter__(self):
        return self
    async def __anext__(self):
        if self.i >= self.limit:
            raise StopAsyncIteration
        self.i = self.i + 1
        return self.i

class AsyncContext:
    async def __aenter__(self):
        return 5
    async def __aexit__(self, exc_type, exc, tb):
        return False

async def async_main():
    total = await add(2, 3)
    async for item in AsyncCounter(3):
        total = total + item
    async with AsyncContext() as value:
        total = total + value
    return total

print(asyncio.run(async_main()))

# Async generators expose async iteration.
async def agen():
    yield 4
    yield 6

async def async_gen_main():
    total = 0
    async for item in agen():
        total = total + item
    return total

print(asyncio.run(async_gen_main()))
