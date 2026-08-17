<!--
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->
# Python 3.14 Compatibility Audit

Status: living checklist

Goal:

```text
XLang3 should become fully compatible with Python 3.14 syntax and runtime behavior,
while keeping the XLang3 runtime, X3Value/X::Value model, refcount/object model,
ProgramIR, and XlangVM execution architecture.
```

Legend:

- [x] implemented enough for current tests
- [~] partial
- [ ] missing or not audited yet

## Syntax Compatibility

### Module And Statement Syntax

- [x] `.py` source files
- [x] indentation-based blocks
- [x] comments
- [x] simple statements on separate lines
- [x] semicolon-separated simple statements
- [x] line continuation with backslash
- [x] implicit line continuation across brackets for multi-line expressions
- [x] `if`
- [x] `else`
- [x] `elif`
- [x] `while`
- [x] `for`
- [x] `break`
- [x] `continue`
- [x] `pass`
- [x] `return`
- [x] `raise expr`
- [x] bare `raise`
- [x] `raise ... from ...` syntax and cause expression evaluation
- [ ] exception chaining metadata for `raise ... from ...`
- [x] `try`
- [x] `except`
- [x] `except E as e`
- [x] `finally`
- [x] `try` / `except` / `else`
- [x] `try` / `finally` / `else`
- [x] `with expr as name`
- [x] multiple context managers in one `with`
- [x] parenthesized multi-line `with`
- [x] `import name`
- [x] `import name as alias`
- [x] `import package.module`
- [x] `from module import name`
- [x] `from module import name as alias`
- [x] `from module import *`
- [x] relative import syntax: `from . import x`
- [x] relative import syntax: `from ..pkg import x`
- [ ] package-context exact relative import resolution
- [x] `global`
- [x] `nonlocal`
- [x] `del`
- [x] `assert`
- [x] `match` / `case` literal-expression and wildcard cases
- [ ] full structural pattern matching semantics
- [x] type parameter syntax accepted on `def` / `class`
- [ ] type parameter runtime metadata

### Function And Class Syntax

- [x] `def f(...):`
- [x] positional parameters
- [x] default parameter values
- [x] keyword-only parameters
- [x] positional-only marker `/`
- [x] varargs `*args`
- [x] kwargs `**kwargs`
- [x] parameter annotations with function `__annotations__` metadata
- [x] return annotations with function `__annotations__` metadata
- [x] decorators on functions, including native callable decorators
- [x] decorators on classes
- [x] nested functions
- [x] closures
- [x] `class C:`
- [x] base classes: `class C(Base):`
- [~] multiple base classes syntax accepted; MRO semantics pending
- [~] metaclass keyword syntax accepted/evaluated; metaclass semantics pending
- [x] class decorators
- [x] `async def`
- [x] `await expr`
- [~] `async for` syntax accepted; async iterator protocol pending
- [~] `async with` syntax accepted; async context manager protocol pending
- [~] generators: `yield` with generator object and iteration; suspended-frame semantics pending
- [~] generators: `yield from` over iterable values; suspended delegation semantics pending
- [ ] async generators
- [x] lambda expressions

### Expression Syntax

- [x] names
- [x] integer literals
- [x] floating-point literals
- [x] string literals
- [~] string escapes
- [ ] raw strings
- [ ] bytes literals
- [ ] f-strings
- [ ] unicode escape completeness
- [x] `None`
- [x] `True` / `False`
- [x] unary `+`
- [x] unary `-`
- [x] `not`
- [x] binary `+`
- [x] binary `-`
- [x] binary `*`
- [x] binary `/`
- [x] binary `%`
- [ ] floor division `//`
- [ ] power `**`
- [ ] bitwise `&`
- [ ] bitwise `|`
- [ ] bitwise `^`
- [ ] shifts `<<` / `>>`
- [ ] unary bit invert `~`
- [x] comparisons `== != < <= > >=`
- [ ] chained comparisons
- [ ] `is`
- [ ] `is not`
- [ ] `in`
- [ ] `not in`
- [x] boolean `and`
- [x] boolean `or`
- [ ] conditional expression `a if cond else b`
- [x] calls: positional args
- [x] calls: keyword args
- [x] calls: `*args`
- [x] calls: `**kwargs`
- [x] attribute access
- [x] subscript access
- [ ] slices `a[start:stop]`
- [ ] extended slices `a[start:stop:step]`
- [ ] tuple unpacking
- [ ] list unpacking
- [ ] starred expression unpacking
- [x] tuple literals
- [x] list literals
- [x] dict literals
- [x] set literals
- [x] list comprehensions, simple
- [x] list comprehensions with optional `if`
- [ ] nested list comprehensions
- [ ] dict comprehensions
- [ ] set comprehensions
- [ ] generator expressions
- [ ] walrus operator `:=`

### Assignment Syntax

- [x] name assignment
- [x] attribute assignment
- [x] subscript assignment
- [ ] tuple/list unpacking assignment
- [ ] starred assignment
- [ ] augmented assignment `+= -= *= /= %=`
- [ ] augmented assignment for all Python operators
- [ ] annotated assignment
- [ ] assignment expression `:=`

## Runtime Compatibility

### Core Value And Object Model

- [x] universal `X3Value` / `X::Value`
- [x] direct scalar storage for int/double/bool/None
- [x] object-backed strings/containers/functions/classes
- [x] refcounted object model
- [ ] Python-compatible `type`
- [ ] Python-compatible `object`
- [ ] `id`
- [ ] identity behavior audit
- [ ] `isinstance`
- [ ] `issubclass`
- [ ] MRO
- [ ] descriptors
- [ ] properties
- [ ] `__getattr__`
- [ ] `__getattribute__`
- [ ] `__setattr__`
- [ ] `__delattr__`
- [ ] `__slots__` compatibility

### Functions And Calls

- [x] user function calls
- [x] native function calls
- [x] bound method calls
- [x] class constructor calls
- [x] nested function calls
- [x] closure cells
- [ ] default args runtime behavior
- [ ] keyword args runtime behavior
- [ ] varargs/kwargs objects
- [ ] function object attributes: `__name__`, `__module__`, `__defaults__`
- [ ] code objects
- [ ] frame objects
- [ ] traceback objects

### Exceptions

- [x] base exception object foundation
- [x] explicit `raise expr`
- [x] typed `except`
- [x] `except E as e`
- [x] subclass matching
- [x] catchable interpreter/native runtime errors
- [x] `finally` unwind basics
- [ ] exception hierarchy completeness
- [ ] traceback capture
- [ ] exception chaining
- [ ] `raise from`
- [ ] bare `raise`
- [ ] `sys.exc_info`
- [ ] `__traceback__`, `__context__`, `__cause__`

### Containers

- [x] tuple basics
- [x] list basics
- [x] dict basics
- [x] set basics
- [x] range basics
- [~] list methods
- [~] dict methods
- [~] set methods
- [~] string methods
- [ ] tuple methods
- [ ] slicing semantics
- [ ] iteration protocol completeness
- [ ] iterator objects compatibility
- [ ] hashing/equality audit
- [ ] ordering behavior audit
- [ ] views: dict keys/items/values compatibility

### Strings And Unicode

- [x] basic string object
- [x] indexing
- [x] basic concatenation
- [~] selected string methods
- [ ] full Python Unicode behavior
- [ ] encoding/decoding
- [ ] string formatting
- [ ] f-string runtime formatting
- [ ] bytes / bytearray
- [ ] memoryview

### Imports And Modules

- [x] source `.py` imports
- [x] packages with `__init__.py`
- [x] native module import
- [x] native package dynamic library import
- [x] `xlang_` fallback native package naming
- [ ] `sys.modules`
- [ ] module specs: `__spec__`
- [ ] loaders/finders
- [ ] `importlib` compatibility
- [ ] namespace packages
- [ ] relative import semantics
- [ ] zip imports
- [ ] frozen modules

### Builtins

- [x] `print`
- [x] `len`
- [x] `range`
- [~] `type`
- [ ] `object`
- [ ] `bool`
- [ ] `int`
- [ ] `float`
- [ ] `str`
- [ ] `list`
- [ ] `dict`
- [ ] `set`
- [ ] `tuple`
- [ ] `enumerate`
- [ ] `zip`
- [ ] `map`
- [ ] `filter`
- [ ] `sum`
- [ ] `min`
- [ ] `max`
- [ ] `abs`
- [ ] `round`
- [ ] `open` compatibility audit
- [ ] `getattr`
- [ ] `setattr`
- [ ] `hasattr`
- [ ] `dir`
- [ ] `vars`
- [ ] `globals`
- [ ] `locals`
- [ ] `eval`
- [ ] `exec`
- [ ] `compile`

### Standard Modules Needed For Debugpy

Native or runtime-backed foundation:

- [ ] `sys`
- [ ] `time`
- [x] `_thread` subset
- [ ] `atexit`
- [ ] `nt` / `posix`
- [ ] `_stat`
- [ ] `_imp`
- [ ] `_io`
- [ ] `_socket`
- [ ] `select`
- [ ] `_weakref`
- [ ] `_collections`
- [ ] `_queue`

High-level Python modules to run from Python source where possible:

- [~] `threading`
- [ ] `os`
- [ ] `socket`
- [x] `json` through native package, not CPython-compatible package yet
- [ ] `queue`
- [ ] `traceback`
- [ ] `inspect`
- [ ] `runpy`
- [ ] `importlib`
- [ ] `types`
- [ ] `collections`
- [ ] `weakref`
- [ ] `logging`
- [ ] `pathlib`

### Async, Tasks, And Threads

- [x] native `task` module
- [x] minimal `asyncio` facade
- [x] `async def` syntax accepted
- [x] `await` syntax accepted and lowered to IR
- [~] `Await` IR operation
- [ ] real resumable coroutine frames
- [ ] event loop semantics
- [ ] `asyncio` compatibility
- [x] `_thread` subset
- [x] `threading.Thread` subset
- [x] `threading.Lock` subset
- [ ] Python-compatible thread lifecycle details
- [ ] thread-local trace hooks
- [ ] no-GIL data sharing policy finalized

### Filesystem And IO

- [x] runtime VFS abstraction
- [x] basic file object
- [x] host filesystem backend
- [x] Pico flash file store foundation
- [ ] CPython-compatible `open`
- [ ] text/binary modes
- [ ] buffering behavior
- [ ] encoding behavior
- [ ] `io` module
- [ ] path protocol

### Debugger Compatibility

- [ ] Python CLI compatibility for debugpy command shapes
- [ ] `sys.settrace`
- [ ] `sys.gettrace`
- [ ] `threading.settrace`
- [ ] `threading.gettrace`
- [ ] frame objects
- [ ] code objects
- [ ] traceback objects
- [ ] IR source line map
- [ ] line events
- [ ] call events
- [ ] return events
- [ ] exception events
- [ ] breakpoint mapping
- [ ] step over
- [ ] step in
- [ ] step out
- [ ] locals/globals variable inspection
- [ ] evaluate expression in selected frame

## Audit Method

For each item:

1. Add or identify a CPython 3.14 behavior test.
2. Run it with `C:\Python\Python314\python.exe`.
3. Run it with `build\Release\xlang3.exe`.
4. Mark the feature as implemented only when behavior matches for the scoped test.
5. If behavior intentionally differs, document the difference in a separate compatibility note.

Compatibility tests should live under:

```text
tests/compat/python314/
```

Each test should be ordinary `.py` wherever possible so the same file can run under CPython and XLang3.
