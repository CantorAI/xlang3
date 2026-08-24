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

Important audit rule:

```text
An importable module, placeholder class, return-none stub, identity decorator,
or empty facade is not Python compatibility. Mark it [ ] unless a declared
CPython-compatible behavior subset has tests.
```

Section-level fixture coverage:

- `tests/fixtures/compat_sections/module_and_statement_syntax.py`
- `tests/fixtures/compat_sections/function_and_class_syntax.py`
- `tests/fixtures/compat_sections/expression_syntax.py`
- `tests/fixtures/compat_sections/core_value_and_object_model.py`
- `tests/fixtures/compat_sections/functions_and_calls.py`
- `tests/fixtures/compat_sections/containers.py`
- `tests/fixtures/compat_sections/strings_and_unicode.py`
- `tests/fixtures/compat_sections/builtins.py`
- `tests/fixtures/compat_sections/standard_modules.py`

## Syntax Compatibility

### Module And Statement Syntax

- [x] `.py` source files
- [x] indentation-based blocks
- [x] comments
- [x] simple statements on separate lines
- [x] semicolon-separated simple statements
- [x] compound statement simple suites on one line: `def f(): return x`, `class C: pass`, `if x: y`
- [x] line continuation with backslash
- [x] implicit line continuation across brackets for multi-line expressions
- [x] `if`
- [x] `else`
- [x] `elif`
- [x] `while`
- [x] `for`
- [x] `for` tuple/list/starred target unpacking
- [x] `for` / `else`
- [x] `break`
- [x] `continue`
- [x] `pass`
- [x] `return`
- [x] `raise expr`
- [x] bare `raise`
- [x] `raise ... from ...` syntax and cause expression evaluation
- [x] exception chaining metadata for `raise ... from ...`
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
- [x] package-context relative import resolution for package modules
- [x] `global`
- [x] `nonlocal`
- [x] `del`
- [x] `assert`
- [x] `match` / `case` literal-expression and wildcard cases
- [x] `match` / `case` soft keyword use in expression, parameter, and definition-name contexts
- [x] structural pattern matching: literal, wildcard, capture, fixed sequence, starred/rest sequence, mapping key/value, OR, `as`, guard basics, class patterns with static/dynamic `__match_args__` and keyword attrs, ordinary failed-pattern capture rollback, and OR-pattern capture-set validation/merge
- [x] type parameter syntax accepted on `def` / `class`
- [x] type parameter runtime metadata basics: `__type_params__` exposes runtime type-parameter objects with `__name__`, `__bound__`, and `__default__`

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
- [x] metaclass keyword syntax accepted/evaluated; callable metaclass factories receive `(name, bases, namespace)`; custom `type` subclasses construct classes with preserved metaclass identity
- [x] class decorators
- [x] `async def`
- [x] `await expr`
- [x] `async for` with `__aiter__` / awaited `__anext__`, `StopAsyncIteration`, `else`, `break`
- [x] `async with` with awaited `__aenter__` / `__aexit__` and exception suppression
- [x] generators: `yield` with suspended XlangVM frame; `send`, `close`, `__next__`, in-frame `throw` injection, and `GeneratorExit` finalization basics
- [x] generators: `yield from` lowered to incremental delegation with generator return-value propagation
- [x] async generators: async-generator functions produce async-iterable generator objects for `async for`; `__anext__`, `asend`, `athrow`, and `aclose` return lazy awaitable helper objects
- [x] lambda expressions

### Expression Syntax

- [x] names
- [x] integer literals
- [x] floating-point literals
- [x] string literals
- [x] string escapes
- [x] raw strings
- [x] string literal lexing edge cases: quote-heavy normal and triple-quoted strings covered in section fixture
- [x] bytes literals
- [x] f-strings: expressions, escaped braces, `!s` / `!r` / `!a`, debug `=`, dynamic specs, and core scalar format specs
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
- [x] Python-compatible `type`: first-class type object, one-arg `type(x)`, and class metaclass identity basics
- [x] Python-compatible `object` root and `object()`
- [x] `id`
- [x] identity behavior audit: object pointer identity plus XLang3 direct-scalar identity policy covered in section fixture
- [x] `isinstance`
- [x] `issubclass`
- [x] MRO
- [x] three-argument `type(name, bases, namespace)` for class creation from tuple bases and dict namespace
- [~] metaclass object model: class creation preserves custom metaclass identity, class calls honor metaclass `__call__`, and type-derived metaclass construction runs metaclass `__prepare__`, `__new__`, and `__init__`; custom namespace mappings, non-class `__new__` return edge cases, and conflict edge cases pending
- [~] descriptors: VM dispatch supports property, slot/member descriptors, and general `__get__` / `__set__` / `__delete__` foundation; CPython edge-case audit pending
- [x] properties: `property(fget, fset, fdel, doc)`, `@property`, `.getter`, `.setter`, `.deleter`, and get/set/delete dispatch covered in section fixture
- [x] `__getattr__` instance hook foundation
- [x] `__getattribute__` instance hook foundation
- [x] `__setattr__` instance hook foundation plus `object.__setattr__`
- [x] `__delattr__` instance hook foundation plus `object.__delattr__`
- [~] `__slots__`: explicit string/list/tuple/set declarations, inherited slot layout for known bases, dynamic attribute restriction, member descriptors, descriptor get/set/delete, deletion, and `__dict__` opt-in basics; weakref behavior and conflict edge cases pending

### Functions And Calls

- [x] user function calls
- [x] native function calls
- [x] bound method calls
- [x] class constructor calls
- [x] nested function calls
- [x] closure cells
- [x] default args runtime behavior
- [x] keyword args runtime behavior: keyword-only/default/`**kwargs` binding and catchable binder `TypeError` covered in section fixture
- [x] varargs/kwargs objects
- [x] function object attributes: `__name__`, `__qualname__`, `__module__`, `__doc__`, positional `__defaults__`, keyword-only `__kwdefaults__`, `__annotations__`, custom attrs, live `__dict__`, `__globals__`, `__closure__`, and `__code__` covered in section fixture
- [~] code objects: foundation with `co_name`, `co_qualname`, `co_argcount`, `co_posonlyargcount`, `co_kwonlyargcount`, `co_nlocals`, `co_stacksize`, signature/generator/coroutine `co_flags`, `co_varnames`, `co_names`, `co_consts`, `co_freevars`, `co_cellvars`, `co_filename`, `co_firstlineno`, `co_code`, `co_linetable`, `co_exceptiontable`, `co_lines()`, and `co_positions()`; remaining CPython code APIs and precise iterator/source-table semantics pending
- [~] frame objects: foundation with `f_code`, `f_back`, `f_globals`, `f_locals`, `f_lasti`, and source-backed `f_lineno`; debugger mutation/source semantics pending
- [~] traceback objects: foundation with `tb_frame`, `tb_next`, `tb_lineno`, and `tb_lasti`; precise source-line mapping pending

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
- [~] list methods: append, extend, insert, pop, clear, copy, count, index, remove, reverse, sort, and `sort(reverse=...)`; key-callable sorting and CPython edge cases pending
- [~] dict methods: get, keys/items/values live views, pop, popitem, setdefault, update-from-dict, update-from-iterable-pairs, keyword update, copy, clear, `fromkeys`, and `|`/`|=` merge basics; CPython edge cases pending
- [~] set methods: add, clear, copy, discard, pop, remove, update, union, intersection, difference, symmetric difference, in-place update methods, subset/superset/disjoint checks, and `|`/`&`/`-`/`^` operators; CPython edge cases pending
- [~] string methods
- [~] tuple methods: `count` and `index`; full CPython edge cases pending
- [x] slicing semantics for list/tuple/string/bytes/bytearray reads plus list/bytearray slice assignment and deletion basics
- [~] iteration protocol completeness: `iter()`, `next()`, default exhaustion value, and lazy iterator basics; protocol hooks pending
- [~] iterator objects compatibility: range/sequence/dict/set/generator plus enumerate/zip/map/filter foundations; full CPython protocol pending
- [~] hashing/equality audit: scalar/string/bytes/object identity, recursive tuple key hashing/equality, mutable-container unhashability, and bool/int key equality covered in section fixture; NaN/custom `__eq__`/`__hash__` edge cases pending
- [~] ordering behavior audit: tuple/list lexicographic comparisons and default list sort basics covered for comparable values; custom comparison and mixed-type edge cases pending
- [~] views: dict keys/items/values compatibility. Live iterable view objects exist for keys, values, and items; set-like view algebra/equality is pending.

### Strings And Unicode

- [x] basic string object
- [x] indexing
- [x] basic concatenation
- [x] selected string methods: case conversion, strip/lstrip/rstrip, find/rfind/index/rindex, count, replace, split, join, partition/rpartition, startswith/endswith, format, encode, and ASCII classification basics covered in fixtures
- [ ] full Python Unicode behavior
- [~] encoding/decoding: UTF-8/ascii `str.encode` and `bytes.decode` basics covered; codec registry, errors handling, and full Unicode codec behavior pending
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
- [x] `type`
- [x] `object`
- [~] `bool`: scalar value with CPython-compatible `type(True) is bool`, `isinstance(True, int)`, and `issubclass(bool, int)` basics; full numeric edge cases pending
- [~] `int`: scalar conversion plus string/bytes/bytearray parsing with explicit base and common prefixes; overflow, `__int__`/`__index__`, and exact CPython error semantics pending
- [~] `float`: scalar and string parsing basics; special values, bytes-like input, and exact CPython edge cases pending
- [~] `str`: object stringification basics; encoding constructor forms and full `__str__` dispatch pending
- [~] `bytes`: bytes-like, iterable-of-int, and zero-filled integer count basics; encoding constructor forms and CPython edge cases pending
- [~] `bytearray`: bytes-like, iterable-of-int, and zero-filled integer count basics; encoding constructor forms and CPython edge cases pending
- [~] `memoryview`: bytes/bytearray/memoryview construction and length basics; full buffer protocol pending
- [~] `list`: iterable constructor basics; subclass/iterator edge cases pending
- [~] `dict`: mapping/pair iterable constructor basics; keyword constructor form and CPython edge cases pending
- [~] `set`: iterable constructor basics; subclass/iterator edge cases pending
- [~] `tuple`: iterable constructor basics; subclass/iterator edge cases pending
- [~] `enumerate`: lazy iterator object foundation; CPython edge cases pending
- [~] `zip`: lazy iterator object foundation; CPython edge cases pending
- [~] `map`: lazy iterator object foundation; CPython edge cases pending
- [~] `filter`: lazy iterator object foundation; CPython edge cases pending
- [x] `sum`
- [x] `min`
- [x] `max`
- [x] `abs`
- [x] `pow`: two-argument numeric form plus int-only modular form basics
- [x] `divmod`: numeric helper backed by floor-div/mod operations
- [~] `round`: numeric basics; CPython edge cases pending
- [x] `hash`: shared hashability/equality policy for scalar/string/bytes/object identity and tuples; mutable containers raise `TypeError`
- [x] `chr`: valid Unicode code point to UTF-8 string, invalid range raises `ValueError`
- [x] `ord`
- [x] `bin`
- [x] `oct`
- [x] `hex`
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

### Standard Modules Foundation

Native or runtime-backed foundation:

- [~] `sys`: `modules` and `exc_info` basics
- [~] `time`: `time`, `time_ns`, `monotonic`, `monotonic_ns`, `perf_counter`, `perf_counter_ns`, `sleep`
- [x] `_thread` subset
- [~] `abc` / `_abc`: ABC cache-token/register/check facade; real ABC registry/cache semantics pending
- [~] `atexit`: native callback registry with `register`, `unregister`, `_run_exitfuncs`; keyword args and full shutdown reporting pending
- [~] `nt` / `posix`: alias to the native `os` module foundation on the host platform
- [~] `_stat`: stat tuple indexes and common file mode constants
- [~] `_imp`: import-lock stubs, `is_builtin`, `is_frozen`, `get_magic`, `extension_suffixes`
- [~] `_io`: module exposes VFS-backed `open`; concrete CPython IO type hierarchy pending
- [~] `_socket`: constants and socket object lifecycle facade; native networking pending
- [~] `_signal`: signal constants and `signal`/`getsignal` facade; real signal delivery semantics pending
- [~] `select`: `select()` shape for non-network readiness lists; native descriptor polling pending
- [~] `_weakref`: `ref`, `proxy`, `ReferenceType`, `ProxyType`, `getweakrefcount`, `getweakrefs` facade; true weak lifetime/callback semantics pending
- [~] `_collections`: native `deque` foundation with common mutating methods; iteration/full CPython semantics pending
- [~] `_queue`: native `SimpleQueue` foundation with put/get/qsize/empty; blocking semantics pending

High-level modules currently backed by native/runtime code:

- [~] `threading`
- [~] `os`: VFS-backed `listdir`, `scandir`/`DirEntry` foundation, `remove`/`unlink`, `stat`, `getcwd`, `chdir`, plus `getenv`/`fspath` basics; full stat/scandir/path-like/error semantics pending
- [~] `os.path` / `ntpath` / `posixpath`: path string helpers foundation; full path normalization/platform semantics pending
- [~] `stat`: stat tuple indexes and common constants
- [ ] `argparse`: simple `ArgumentParser` facade exists; CPython parser behavior not audited
- [ ] `ast`: AST class-name facade exists; real parser-to-AST compatibility pending
- [ ] `code`: `compile_command` facade exists; interactive compiler semantics pending
- [~] `codecs`: `lookup`, `encode`, and `decode` foundation; full codec registry/error handling pending
- [ ] `contextlib`: contextmanager/closing/suppress facade exists; generator context-manager behavior pending
- [ ] `ctypes`: small facade exists; real FFI semantics pending
- [ ] `dataclasses`: decorator/field facade exists; real dataclass transformation pending
- [ ] `dis`: code-object inspection facade exists; real bytecode/disassembly compatibility pending
- [~] `enum`: native foundation for `Enum`, `IntEnum`, `IntFlag`, `Flag`, `StrEnum`, `auto`, and decorators; real enum metaclass/member semantics pending
- [~] `fnmatch` / `glob`: filename matching helpers foundation; recursive glob/path edge cases pending
- [~] `functools`: `update_wrapper`, `wraps`, and `partial` foundation with common function metadata propagation; full descriptor/cache/singledispatch/cmp helpers pending
- [ ] `__future__`: feature-name constants facade only
- [ ] `getpass`: `getpass`/`getuser` facade only
- [~] `itertools`: selected iterator helpers foundation; full iterator algebra pending
- [~] `json`: native `loads`/`load`/`dumps`/`save` foundation; full CPython `json` package behavior pending
- [ ] `locale`: constants and encoding helper facade; real locale behavior pending
- [ ] `marshal`: placeholder module; serialization semantics pending
- [~] `numbers`: numeric ABC facade; real ABC registration/virtual subclass integration pending
- [ ] `opcode`: opcode metadata facade; CPython opcode compatibility pending
- [~] `operator`: selected helpers such as `index` and `getitem`; full operator module pending
- [ ] `pickle`: exception/classes facade; serialization semantics pending
- [~] `platform`: platform/python version helpers foundation
- [ ] `pkgutil`: package utility facade; loader/resource semantics pending
- [~] `re`: regex compile/match/search/fullmatch/escape facade; full CPython regex semantics pending
- [ ] `signal`: public signal facade over `_signal`; real delivery semantics pending
- [ ] `site`: site-package path helper facade; startup-site behavior pending
- [~] `socket`: facade over `_socket` constants and socket object basics; connect/bind/send/recv pending
- [~] `queue`: facade over native `SimpleQueue`; full Queue/Empty/Full/blocking semantics pending
- [~] `string`: public constants and importable `Formatter` type foundation; full `Formatter` behavior pending
- [~] `struct`: `calcsize`, `pack`, `unpack` foundation; full format compatibility pending
- [ ] `subprocess`: constants and `Popen`/run facade; real process piping and lifecycle semantics pending
- [ ] `sysconfig`: path/config helper facade; full install scheme compatibility pending
- [ ] `typing`: selected aliases/decorators facade; parsed type-parameter bounds/defaults/variance/lazy evaluation and full typing runtime behavior pending
- [~] `traceback`: `format_exception`, `format_exception_only`, `format_exc`, `print_exception` basics; exact frame/line formatting pending
- [~] `inspect`: common predicates, `currentframe`/`stack` placeholders, `getfile`, and basic `getmembers`; full frame/source/signature semantics pending
- [~] `runpy`: `run_module` and `run_path` basics returning globals dict snapshots
- [~] `importlib`: `import_module`, `invalidate_caches`, and `importlib.util.find_spec` basics
- [~] `types`: `ModuleType`, `SimpleNamespace`, `MethodType` basics; exact CPython type objects pending
- [~] `collections`: facade exposing native `deque`; Counter/defaultdict/namedtuple/etc. pending
- [~] `weakref`: facade over `_weakref` basics plus `finalize` placeholder; true weak lifetime/callback semantics pending
- [~] `logging`: native logger facade with levels, `basicConfig`, root functions, `getLogger`, and Logger methods; handler/formatter hierarchy pending
- [~] `pathlib`: `Path`/`PurePath` facade with VFS-backed exists/read/write checks, CPython-style `name`/`suffix`/`parent` properties, and basic path transforms; full operator/glob semantics pending
- [~] `urllib.parse`: quote/unquote helper foundation; full URL parsing pending
- [~] `warnings` / `_warnings`: `warn`, `warn_explicit`, `simplefilter`, `filterwarnings`, `resetwarnings`, and `catch_warnings(record=True)` recording basics; filter/category/showwarning semantics pending
- [ ] `winreg`: Windows constant facade; real registry operations pending
- [ ] `xmlrpc` / `http` package placeholders: module/package import foundation only

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
- [~] file object: read/write/close/context manager plus read(size), readline(s), writelines, seek/tell, closed; iterator/newline/full errors pending
- [x] host filesystem backend
- [x] Pico flash file store foundation
- [~] CPython-compatible `open`: VFS path/path-like input and `r/w/a/x/+` mode parsing; full error classes/opener semantics pending
- [~] text/binary modes: text strings and binary bytes/bytearray for core read/write paths
- [~] buffering behavior: `buffering` keyword is accepted and validated; buffering policy is still VFS-buffer based
- [~] encoding behavior: UTF-8 text path accepted through `encoding`/`errors`/`newline` keywords; codec conversion and newline translation pending
- [~] `io` module: `_io` and `io` expose `open`, IO base type placeholders, `StringIO`, and `BytesIO`; full CPython hierarchy pending
- [~] path protocol: `open(Path(...))`, `os.fspath(Path(...))`, and `Path.__fspath__` basics

### Debugger Compatibility

- [~] Python CLI compatibility: Windows `python.exe` alias, script args, `-c`, `-m`, directory `__main__.py`, ignored safe `-X` flags, `sys.argv`, and live `sys.path` import search; full CPython flag matrix pending
- [~] `sys.settrace`: hook storage plus Python function call/line/return/exception event dispatch; CPython edge cases pending
- [~] `sys.gettrace`: returns stored hook
- [~] `threading.settrace`: default thread hook storage foundation; native thread propagation/events pending
- [~] `threading.gettrace`: returns stored default hook
- [~] frame objects: `inspect.currentframe()`, `f_back`, `f_code`, `f_globals`, `f_locals`, and source-backed `f_lineno` foundation
- [~] code objects: debugger-visible `co_filename` and `co_firstlineno` added; full CPython code metadata pending
- [~] traceback objects
- [x] IR source line map: parser statement line stamps lower to per-instruction line metadata and serialize through IR cache
- [~] line events: source-backed line events use per-frame local trace functions
- [~] call events: Python function calls emit trace call events; native/builtin call event policy pending
- [~] return events: Python function returns emit trace return events; generator/exception edge cases pending
- [~] exception events: raised Python exceptions emit trace exception events before handler/unwind dispatch
- [~] VM debug poll gate: debugger hook is runtime-disabled by default; breakpoint/step checks activate through a cached poll-needed flag
- [~] VM debug pause/resume: breakpoint/step hits can preserve the XlangVM frame stack and resume from the same instruction; host protocol binding pending
- [~] debug session controller: desktop runtime API owns loaded source, breakpoints, pause status, continue, step in/over/out, and pause request; native DAP is the product transport
- [~] native DAP session: C++ DAP framing plus initialize/launch/setBreakpoints/setExceptionBreakpoints/configurationDone/continue/step/threads/stack/scopes/variables over `DebugSession`; `xlang3 --dap-stdio` host, initialized/output/terminated events, frame-chain stack trace, and locals/globals scopes added; socket host pending
- [~] VS Code native DAP registration: minimal `tools/vscode/xlang3-debug` extension starts `xlang3 --dap-stdio`; manual IDE validation pending
- [~] Visual Studio 2026 native DAP smoke: VS Debug Adapter Host launched `xlang3 --dap-stdio`, stopped at entry, continued, and observed clean adapter exit; packaged VSIX/project-system integration pending
- [~] breakpoint mapping: private VM hook and pause state support filename/line breakpoint hits and native DAP binding
- [~] step over: VM policy skips deeper frames and pauses at the next source line in the original/caller frame; native DAP binding added
- [~] step in: private VM hook and pause state support source-line step-into hits; native DAP binding added
- [~] step out: VM policy pauses after the selected frame returns to its caller; native DAP binding added
- [~] pause request: VM can stop at the next source line without a breakpoint; external debugger request channel pending
- [~] locals/globals variable inspection: current frame snapshots expose locals/globals dicts; debugger mutation/watch semantics pending
- [~] evaluate expression in selected frame: native DAP parses Python expressions and evaluates names/attrs/indexing/calls/literals/containers/basic operators against paused frame locals/globals; full VM eval mode, mutation, keyword calls, and all Python expression forms pending

## Recent Compatibility Debt

These items are useful Python 3.14 compatibility work, but they must not be
considered complete until CPython-vs-XLang3 tests exist for the declared scope.

- [ ] `enum` audit:
  current native module is a foundation. It must be tested against CPython for
  member creation, value lookup, aliases, `auto()`, `IntEnum`, `IntFlag`, `Flag`
  operators, decorators such as `unique`, class attributes, `repr`, `str`, and
  pickling-facing helpers.

- [ ] inherited builtin constructor audit:
  subclasses of `int`, `str`, `float`, and `bytes` can route through builtin
  constructors. This needs tests for normal subclass construction,
  class-level constants, `isinstance`, arithmetic/string behavior, and whether
  the result should be base scalar or subclass instance in each Python case.

- [ ] `os.scandir` / `DirEntry` audit:
  current implementation materializes a list-like result and minimal `DirEntry`
  objects. CPython returns a scandir iterator/context manager. Tests must cover
  iterator behavior, context manager cleanup, `name`, `path`, `inode`,
  `is_dir(follow_symlinks=...)`, `is_file(follow_symlinks=...)`, `is_symlink`,
  `stat`, path-like arguments, bytes paths, and error classes.

- [~] function metadata audit:
  Covered in fixtures: docstrings, positional `__defaults__`, keyword-only
  `__kwdefaults__`, direct assignment for those defaults, annotations
  assignment, custom function attrs, explicit assignment through live
  `__dict__`, `__globals__`, `__closure__`, `__code__`, and `__qualname__`
  basics for module functions, nested functions, class methods, and nested
  class methods. Still pending: CPython-exact code object completeness,
  bound-method metadata, static methods, class methods, and native functions.

- [ ] tokenizer/string-literal audit:
  lexer now avoids treating triple quote sequences inside normal strings as
  triple-string openers. Tests must cover raw strings, bytes strings, f-strings,
  adjacent literals, triple strings after expressions, comments, escaped quotes,
  and CPython `Lib/tokenize.py`.

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
