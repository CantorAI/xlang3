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

# User, native, bound method, constructor, nested function, and closure calls.
def user_add(a, b):
    return a + b

class Counter:
    def __init__(self, start):
        self.value = start

    def add(self, amount):
        self.value = self.value + amount
        return self.value

def make_adder(base):
    def add(value):
        return base + value
    return add

counter = Counter(3)
print(user_add(2, 4), len([1, 2, 3]), counter.add(5), make_adder(10)(7))

# Defaults are evaluated at function definition time and reused.
default_marker = 1
def defaults(a, b=default_marker, items=[]):
    items.append(a)
    return b, items

default_marker = 99
print(defaults(2))
print(defaults(3))

# Positional-only, keyword-only, *args, **kwargs, * expansion, and ** expansion.
def binder(a, /, b, *rest, c=4, **kw):
    return a, b, rest, c, kw["name"]

more = (3, 4)
named = {"name": "ok"}
print(binder(1, 2, *more, c=5, **named))
print(user_add(*"ab"))
try:
    binder(a=1, b=2, name="bad")
except TypeError:
    print("posonly-catchable")

def requires_kwonly(*, needed):
    return needed

print(requires_kwonly(needed="kw"))
try:
    requires_kwonly()
except TypeError:
    print("kwonly-catchable")

# Function metadata and live function __dict__.
def meta(a, b=8, *, c=9):
    "meta-doc"
    return a + b + c

print(meta.__name__, meta.__qualname__, meta.__module__)
print(meta.__doc__, meta.__defaults__, meta.__kwdefaults__["c"])
print(meta.__globals__["__name__"], meta.__closure__)
meta.__defaults__ = (20,)
meta.__kwdefaults__ = {"c": 30}
print(meta(1))
def two_defaults(a, b=2, c=3):
    return a + b + c

two_defaults.__defaults__ = (10,)
print(two_defaults(1, 2))
two_defaults.__defaults__ = None
try:
    two_defaults(1)
except TypeError:
    print("defaults-none-catchable")

def annotated(x: "int") -> "int":
    return x

print(annotated.__annotations__["x"], annotated.__annotations__["return"])
meta.custom = "attr"
print(meta.__dict__["custom"], vars(meta)["custom"])
meta.__dict__["from_dict"] = "live"
print(meta.from_dict)
del meta.from_dict
try:
    print(meta.from_dict)
except Exception:
    print("function-dict-delete")

# Code objects, frame objects, and traceback object foundation.
def frame_probe():
    import inspect
    frame = inspect.currentframe()
    return frame.f_code.co_name, frame.f_back.f_code.co_name, frame.f_lineno >= frame.f_code.co_firstlineno

print(frame_probe())
print(meta.__code__.co_name, "a" in meta.__code__.co_varnames, "c" in meta.__code__.co_varnames)
print(meta.__code__.co_argcount, meta.__code__.co_kwonlyargcount, meta.__code__.co_nlocals, meta.__code__.co_stacksize > 0)
print(meta.__code__.co_qualname, meta.__code__.co_posonlyargcount, meta.__code__.co_flags)
print(binder.__code__.co_flags, isinstance(meta.__code__.co_code, bytes))
print(frame_probe.__code__.co_linetable, frame_probe.__code__.co_exceptiontable)
print(len(list(meta.__code__.co_lines())) > 0, len(list(meta.__code__.co_positions())) > 0)
replaced_code = meta.__code__.replace(co_filename="custom.py", co_firstlineno=123)
print(replaced_code.co_filename, replaced_code.co_firstlineno, replaced_code.co_name)

def traceback_probe():
    def inner():
        raise ValueError("boom")

    try:
        inner()
    except Exception as exc:
        tb = exc.__traceback__
        last = tb
        while last.tb_next != None:
            last = last.tb_next
        return last.tb_frame.f_code.co_name, last.tb_lasti >= 0

print(traceback_probe())

def frame_extra_probe():
    import inspect
    frame = inspect.currentframe()
    return isinstance(frame.f_builtins, dict), frame.f_trace, frame.f_trace_lines, frame.f_trace_opcodes

print(frame_extra_probe())

def traceback_mutation_probe():
    try:
        raise ValueError("trace")
    except Exception as exc:
        tb = exc.__traceback__
        tb.tb_next = None
        return tb.tb_next is None, tb.tb_lineno > 0

print(traceback_mutation_probe())
