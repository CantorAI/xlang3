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
- [~] exception chaining metadata for `raise ... from ...`
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
- [x] multiple base classes with C3 MRO for tested class lookup
- [~] metaclass keyword syntax accepted/evaluated; metaclass semantics pending
- [x] class decorators
- [x] `async def`
- [x] `await expr`
- [~] `async for` syntax accepted; async iterator protocol pending
- [~] `async with` syntax accepted; async context manager protocol pending
- [~] generators: `yield` with suspended XlangVM frame; `send` / `throw` / `close` pending
- [~] generators: `yield from` lowered to incremental delegation; StopIteration return-value propagation pending
- [ ] async generators
- [x] lambda expressions

### Expression Syntax

- [x] names
- [x] integer literals
- [x] floating-point literals
- [x] string literals
- [x] string escapes
- [x] raw strings
- [x] bytes literals
- [~] f-strings
- [x] unicode escape completeness
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
- [x] floor division `//`
- [x] power `**`
- [x] bitwise `&`
- [x] bitwise `|`
- [x] bitwise `^`
- [x] shifts `<<` / `>>`
- [x] unary bit invert `~`
- [x] comparisons `== != < <= > >=`
- [x] chained comparisons
- [x] `is`
- [x] `is not`
- [x] `in`
- [x] `not in`
- [x] boolean `and`
- [x] boolean `or`
- [x] conditional expression `a if cond else b`
- [x] calls: positional args
- [x] calls: keyword args
- [x] calls: `*args`
- [x] calls: `**kwargs`
- [x] attribute access
- [x] subscript access
- [x] slices `a[start:stop]`
- [x] extended slices `a[start:stop:step]`
- [x] tuple unpacking assignment
- [x] list unpacking assignment
- [x] starred expression unpacking
- [x] tuple literals
- [x] list literals
- [x] dict literals
- [x] set literals
- [x] list comprehensions, simple
- [x] list comprehensions with optional `if`
- [x] nested list comprehensions
- [x] dict comprehensions
- [x] set comprehensions
- [x] generator expressions
- [x] walrus operator `:=`

### Assignment Syntax

- [x] name assignment
- [x] attribute assignment
- [x] subscript assignment
- [x] tuple/list unpacking assignment
- [x] starred assignment
- [x] augmented assignment `+= -= *= /= %=`
- [x] augmented assignment for implemented Python operators
- [x] annotated assignment
- [x] assignment expression `:=`

## Runtime Compatibility

### Core Value And Object Model

- [x] universal `X3Value` / `X::Value`
- [x] direct scalar storage for int/double/bool/None
- [x] object-backed strings/containers/functions/classes
- [x] refcounted object model
- [~] Python-compatible `type`: first-class type object and one-arg `type(x)` basics
- [x] Python-compatible `object` root and `object()`
- [x] `id`
- [ ] identity behavior audit
- [x] `isinstance`
- [x] `issubclass`
- [x] MRO
- [x] three-argument `type(name, bases, namespace)` for class creation from tuple bases and dict namespace
- [ ] full metaclass object model
- [~] descriptors: VM dispatch supports property plus general `__get__` / `__set__` / `__delete__` foundation; CPython edge-case audit pending
- [~] properties: `property(fget, fset, fdel, doc)`, `@property`, `.getter`, `.setter`, `.deleter`, get/set/delete dispatch; CPython edge-case audit pending
- [x] `__getattr__` instance hook foundation
- [x] `__getattribute__` instance hook foundation
- [x] `__setattr__` instance hook foundation plus `object.__setattr__`
- [x] `__delattr__` instance hook foundation plus `object.__delattr__`
- [ ] `__slots__` compatibility

### Functions And Calls

- [x] user function calls
- [x] native function calls
- [x] bound method calls
- [x] class constructor calls
- [x] nested function calls
- [x] closure cells
- [~] default args runtime behavior
- [~] keyword args runtime behavior
- [~] varargs/kwargs objects
- [~] function object attributes: `__name__`, `__module__`, `__defaults__`, `__annotations__`, `__code__`; broader CPython audit pending
- [~] code objects: foundation with `co_name`, `co_argcount`, `co_varnames`, `co_names`, `co_consts`; full CPython fields pending
- [~] frame objects: foundation with `f_code`, `f_globals`, `f_lineno`; locals/source-line semantics pending
- [~] traceback objects: foundation with `tb_frame`, `tb_next`, `tb_lineno`; precise source-line mapping pending

### Exceptions

- [x] base exception object foundation
- [x] explicit `raise expr`
- [x] typed `except`
- [x] `except E as e`
- [x] subclass matching
- [x] catchable interpreter/native runtime errors
- [x] `finally` unwind basics
- [~] exception hierarchy completeness: common built-in exception classes registered; full CPython tree pending
- [~] traceback capture: VM exception path builds frame chain; exact line table pending
- [~] exception chaining: explicit cause and implicit context metadata basics; display formatting pending
- [~] `raise from` runtime cause/context metadata
- [~] bare `raise` runtime behavior inside/outside active exception
- [~] `sys.exc_info`: active exception tuple basics
- [~] `__traceback__`, `__context__`, `__cause__`, `__suppress_context__` basic attributes

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
- [~] tuple methods: `count` and `index`; full CPython edge cases pending
- [ ] slicing semantics
- [~] iteration protocol completeness: `iter()`, `next()`, default exhaustion value, and lazy iterator basics; protocol hooks pending
- [~] iterator objects compatibility: range/sequence/dict/set/generator plus enumerate/zip/map/filter foundations; full CPython protocol pending
- [ ] hashing/equality audit
- [ ] ordering behavior audit
- [~] views: dict keys/items/values compatibility. Live iterable view objects exist for keys, values, and items; set-like view algebra/equality is pending.

### Strings And Unicode

- [x] basic string object
- [x] indexing
- [x] basic concatenation
- [x] selected string methods
- [ ] full Python Unicode behavior
- [ ] encoding/decoding
- [~] string formatting
- [~] f-string runtime formatting
- [~] bytes / bytearray
- [~] memoryview

### Imports And Modules

- [x] source `.py` imports
- [x] packages with `__init__.py`
- [x] native module import
- [x] native package dynamic library import
- [x] `xlang_` fallback native package naming
- [x] `sys.modules` runtime-maintained module registry dict
- [~] module specs: `__spec__` placeholder exposed as `None`; real specs/loaders pending
- [ ] loaders/finders
- [ ] `importlib` compatibility
- [ ] namespace packages
- [ ] relative import semantics
- [ ] zip imports
- [ ] frozen modules

### Builtins

- [x] `print`
- [x] `len`
- [x] `iter`
- [~] `next`: default value and `StopIteration` class basics; exact exception payload semantics pending
- [x] `range`
- [~] `type`: object plus one-arg call; three-arg dynamic class creation pending
- [x] `object`
- [~] `bool`
- [~] `int`
- [~] `float`
- [~] `str`
- [~] `bytes`
- [~] `bytearray`
- [~] `memoryview`
- [~] `list`
- [~] `dict`
- [~] `set`
- [~] `tuple`
- [~] `enumerate`: lazy iterator object foundation; CPython edge cases pending
- [~] `zip`: lazy iterator object foundation; CPython edge cases pending
- [~] `map`: lazy iterator object foundation; CPython edge cases pending
- [~] `filter`: lazy iterator object foundation; CPython edge cases pending
- [x] `sum`
- [x] `min`
- [x] `max`
- [x] `abs`
- [~] `round`: numeric basics; CPython edge cases pending
- [~] `open`: VFS-backed text file basics and context-manager methods; encoding/binary/buffering semantics pending
- [x] `getattr`
- [x] `setattr`
- [x] `hasattr`
- [~] `dir`: module/class/instance basics
- [~] `vars`: module/class/instance snapshot basics
- [~] `globals`: active module snapshot; live dict semantics pending
- [~] `locals`: active frame snapshot foundation
- [~] `eval`: string/code-object expression basics using current globals
- [~] `exec`: string/code-object statement basics using current globals
- [~] `compile`: `exec`/`eval`/`single` code-object basics
- [x] `callable`

### Standard Modules Needed For Debugpy

Native or runtime-backed foundation:

- [~] `sys`: `modules` and `exc_info` basics
- [~] `time`: `time`, `time_ns`, `monotonic`, `monotonic_ns`, `perf_counter`, `perf_counter_ns`, `sleep`
- [x] `_thread` subset
- [~] `atexit`: native callback registry with `register`, `unregister`, `_run_exitfuncs`; keyword args and full shutdown reporting pending
- [~] `nt` / `posix`: alias to the native `os` module foundation on the host platform
- [~] `_stat`: stat tuple indexes and common file mode constants
- [~] `_imp`: import-lock stubs, `is_builtin`, `is_frozen`, `get_magic`, `extension_suffixes`
- [~] `_io`: module exposes VFS-backed `open`; concrete CPython IO type hierarchy pending
- [ ] `_socket`
- [ ] `select`
- [ ] `_weakref`
- [~] `_collections`: native `deque` foundation with common mutating methods; iteration/full CPython semantics pending
- [~] `_queue`: native `SimpleQueue` foundation with put/get/qsize/empty; blocking semantics pending

High-level Python modules to run from Python source where possible:

- [~] `threading`
- [~] `os`: VFS-backed `listdir`, `remove`/`unlink`, `stat`, `getcwd`, `chdir`, plus `getenv`/`fspath` basics
- [ ] `socket`
- [x] `json` through native package, not CPython-compatible package yet
- [~] `queue`: facade over native `SimpleQueue`; full Queue/Empty/Full/blocking semantics pending
- [ ] `traceback`
- [ ] `inspect`
- [ ] `runpy`
- [ ] `importlib`
- [~] `types`: `ModuleType`, `SimpleNamespace`, `MethodType` basics; exact CPython type objects pending
- [~] `collections`: facade exposing native `deque`; Counter/defaultdict/namedtuple/etc. pending
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
- [~] frame objects
- [~] code objects
- [~] traceback objects
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

Current checkpoint tests live under:

```text
tests/fixtures/core/
tests/fixtures/expected/
```

Dedicated CPython-vs-XLang3 compatibility tests should live under:

```text
tests/compat/python314/
```

Each test should be ordinary `.py` wherever possible so the same file can run under CPython and XLang3.
