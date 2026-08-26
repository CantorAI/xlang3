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
- [x] class variable annotations populate class `__annotations__`
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
- [x] metaclass object model: class creation preserves custom metaclass identity, class calls honor metaclass `__call__`, type-derived metaclass construction runs metaclass `__prepare__`, `__new__`, and `__init__`, prepared dict-subclass namespaces are accepted, non-class `__new__` returns skip metaclass `__init__`, and base metaclass inheritance/conflict checks are covered in section fixture
- [x] descriptors: VM dispatch supports property, slot/member descriptors, general `__get__` / `__set__` / `__delete__`, data-descriptor precedence over instance attributes, and non-data descriptor instance override behavior
- [x] properties: `property(fget, fset, fdel, doc)`, `@property`, `.getter`, `.setter`, `.deleter`, and get/set/delete dispatch covered in section fixture
- [x] `__getattr__` instance hook foundation
- [x] `__getattribute__` instance hook foundation
- [x] `__setattr__` instance hook foundation plus `object.__setattr__`
- [x] `__delattr__` instance hook foundation plus `object.__delattr__`
- [x] user-defined `__len__`, `__getitem__`, `__setitem__`, and `__delitem__` fallback dispatch after native sequence/mapping fast paths
- [x] `__slots__`: explicit string/list/tuple/set declarations, inherited slot layout for known bases, dynamic attribute restriction, member descriptors, descriptor get/set/delete, deletion, `__dict__` opt-in basics, slotted weakref eligibility, and slot/class-variable conflict validation

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
- [x] code objects: XLang3 IR-backed code objects expose `co_name`, `co_qualname`, `co_argcount`, `co_posonlyargcount`, `co_kwonlyargcount`, `co_nlocals`, `co_stacksize`, signature/generator/coroutine `co_flags`, `co_varnames`, `co_names`, `co_consts`, `co_freevars`, `co_cellvars`, `co_filename`, `co_firstlineno`, bytes-shaped `co_code`, `co_linetable`, `co_exceptiontable`, iterable `co_lines()` / `co_positions()`, and keyword `replace(...)` for common code metadata
- [x] frame objects: expose `f_code`, `f_back`, `f_globals`, `f_builtins`, `f_locals`, `f_lasti`, source-backed `f_lineno`, and debugger-facing `f_trace`, `f_trace_lines`, and `f_trace_opcodes` fields
- [x] traceback objects: expose `tb_frame`, writable `tb_next`, `tb_lineno`, and `tb_lasti` over XLang3 frame/source metadata

### Exceptions

- [x] base exception object foundation
- [x] explicit `raise expr`
- [x] typed `except`
- [x] `except E as e`
- [x] subclass matching
- [x] catchable interpreter/native runtime errors
- [x] `finally` unwind basics
- [x] exception hierarchy completeness: Python 3.14 built-in exception classes and aliases registered with CPython-style subclass relationships
- [x] traceback capture: VM exception path builds frame chains with frame/code names and source-backed line numbers
- [x] exception chaining: explicit cause and implicit context metadata
- [x] `raise from` runtime cause/context metadata, including `from None` suppression
- [x] bare `raise` runtime behavior inside/outside active exception
- [x] `sys.exc_info`: active exception tuple behavior
- [x] `__traceback__`, `__context__`, `__cause__`, `__suppress_context__` basic attributes

### Containers

- [x] tuple basics
- [x] list basics
- [x] dict basics
- [x] set basics
- [x] range basics
- [x] list methods: append, extend, insert, pop, clear, copy, count, index, remove, reverse, stable sort, `sort(reverse=...)`, and key-callable sorting
- [x] dict methods: get, keys/items/values live views, pop, popitem, setdefault, update-from-dict, update-from-iterable-pairs, keyword update, copy, clear, `fromkeys`, and `|`/`|=` merge behavior
- [x] set methods: add, clear, copy, discard, pop, remove, update, union, intersection, difference, symmetric difference, in-place update methods, subset/superset/disjoint checks, and `|`/`&`/`-`/`^` operators
- [x] string methods are tracked in the dedicated Strings And Unicode section
- [x] tuple methods: `count` and `index`
- [x] slicing semantics for list/tuple/string/bytes/bytearray reads plus list/bytearray slice assignment and deletion basics
- [x] iteration protocol completeness: `iter()`, `next()`, default exhaustion value, and lazy iterator basics
- [x] iterator objects compatibility: range/sequence/dict/set/generator plus enumerate/zip/map/filter foundations
- [x] hashing/equality audit: scalar/string/bytes/object identity, recursive tuple key hashing/equality, mutable-container unhashability, and bool/int key equality covered in section fixture
- [x] ordering behavior audit: tuple/list lexicographic comparisons and stable list sort with key/reverse for comparable values
- [x] views: dict keys/items/values compatibility. Live iterable view objects exist for keys, values, and items; keys/items support set-like equality and `|`/`&`/`-`/`^` algebra; values-view equality follows identity semantics.

### Strings And Unicode

- [x] basic string object
- [x] indexing
- [x] basic concatenation
- [x] string methods: case conversion, `capitalize`, `casefold`, `swapcase`, `title`/`istitle`, strip/lstrip/rstrip, find/rfind/index/rindex, count, replace, split/rsplit/splitlines, join, partition/rpartition, startswith/endswith tuple prefixes, padding, zfill, prefix/suffix removal, expandtabs, format, encode, and ASCII classification covered in section fixture
- [x] Unicode scalar behavior: UTF-8 `str` length, integer indexing, negative indexing, slicing, and `ord()` over non-ASCII code points covered in section fixture
- [x] encoding/decoding: UTF-8/ascii `str.encode` and `bytes`/`bytearray.decode` basics plus catchable Unicode encode/decode errors covered
- [x] string formatting
- [x] f-string runtime formatting
- [x] bytes / bytearray: constructors, indexing/slicing, mutation, startswith/endswith tuple prefixes, partition/rpartition, split/join, count/find/index/rfind/rindex, strip/lstrip/rstrip, replace, hex, decode, copy, append/extend/pop/remove/reverse/clear, and raw `\xNN` bytes-literal escapes covered
- [x] memoryview: construction over bytes-like storage, indexing, `tobytes`, `tolist`, and core read-only/shape metadata attributes covered
- [~] deep Unicode database behavior: native `unicodedata` foundation now covers lookup/name and selected
  name aliases, category, bidirectional, combining class, East Asian width, mirrored, decimal/digit/numeric,
  decomposition, and NFC/NFD/NFKC/NFKD normalization for the current table-driven core set; codec paths now cover alias-normalized lookup,
  getencoder/getdecoder, CodecInfo encode/decode callables, error-handler lookup/registration foundation,
  and strict/ignore/replace/backslashreplace basics for UTF-8/UTF-8-SIG/ASCII/Latin-1; complete
  generated Unicode tables, locale-sensitive casing, grapheme-cluster text segmentation, identifier edge
  cases, and the full codec registry/error-handler matrix remain tracked for the dedicated Unicode engine pass

### Imports And Modules

- [x] source `.py` imports
- [x] packages with `__init__.py`
- [x] native module import
- [x] native package dynamic library import
- [x] `xlang_` fallback native package naming
- [x] `sys.modules` runtime-maintained module registry dict
- [x] module specs: native, source, package, namespace, and zip-source modules expose real `__spec__`, `__loader__`, `origin`, `parent`, `has_location`, and package search-location metadata
- [x] loaders/finders: `importlib.abc` and `importlib.machinery` expose common loader/finder classes; `SourceFileLoader` supports `create_module`, `exec_module`, `get_filename`, and `get_data`; `PathFinder.find_spec` returns specs for importable modules
- [x] `importlib` compatibility: `import_module`, relative `import_module`, `invalidate_caches`, `util.find_spec`, `spec_from_file_location`, `module_from_spec`, explicit loader execution, and metadata distribution facade basics covered
- [x] namespace packages: no-`__init__.py` package import, child binding, list-shaped `__path__`, importlib spec basics, and multi-root path merging covered
- [x] relative import semantics: parser syntax, package-context resolution, and `importlib.import_module(..., package=...)` basics covered
- [x] zip imports: `zipimport` facade, `zipimporter` protocol basics, native stored/deflated-entry ZIP `get_data`, and `sys.path` zip source module execution covered
- [x] frozen modules: `_frozen_importlib`, `_frozen_importlib_external`, and importlib bootstrap aliases expose the runtime bootstrap/import protocol facades needed by Python libraries
- [~] CPython import internals intentionally deferred: `.pyc` cache execution, encrypted ZIP imports, exact import-lock edge cases, and CPython's frozen bytecode table are tracked separately from source-compatible import behavior

### Builtins

- [x] `print`
- [x] `len`
- [x] `iter`
- [x] `next`: iterator advancement, default value, and `StopIteration` class basics
- [x] `range`
- [x] `type`
- [x] `object`
- [x] `bool`: scalar value with CPython-compatible `type(True) is bool`, `isinstance(True, int)`, and `issubclass(bool, int)` basics
- [x] `int`: scalar conversion plus string/bytes/bytearray parsing with explicit base and common prefixes
- [x] `float`: scalar, string, bytes, and bytearray parsing basics
- [x] `str`: object stringification and bytes-like decoding constructor forms
- [x] `bytes`: bytes-like, iterable-of-int, zero-filled integer count, and encoded string constructor forms
- [x] `bytearray`: bytes-like, iterable-of-int, zero-filled integer count, and encoded string constructor forms
- [~] `memoryview`: bytes/bytearray/memoryview construction, length/index/slice basics, tuple-of-one indexing, readonly and shape metadata,
  `tobytes(order)`, `tolist`, `hex` separators, byte-sized `cast` with tuple/list one-dimensional shape, `toreadonly`,
  3.14 `count`/`index`, `release`, context manager release behavior, writable bytearray-backed item/slice assignment,
  bytes-like equality foundations, and readonly byte-format hashing aligned with bytes; full multi-format/multi-dimensional
  buffer protocol, exporter resize locking, and exact release exception typing pending
- [x] `list`: iterable constructor basics
- [x] `dict`: mapping/pair iterable constructor plus keyword and expanded keyword forms
- [x] `set`: iterable constructor basics
- [x] `tuple`: iterable constructor basics
- [x] `enumerate`: lazy iterator object with `start`
- [x] `zip`: lazy iterator object
- [x] `map`: lazy iterator object
- [x] `filter`: lazy iterator object
- [x] `sum`
- [x] `min`
- [x] `max`
- [x] `abs`
- [x] `pow`: two-argument numeric form plus int-only modular form basics
- [x] `divmod`: numeric helper backed by floor-div/mod operations
- [x] `round`: numeric basics with optional `ndigits`
- [x] `hash`: shared hashability/equality policy for scalar/string/bytes/object identity and tuples; mutable containers raise `TypeError`
- [x] `chr`: valid Unicode code point to UTF-8 string, invalid range raises `ValueError`
- [x] `ord`
- [x] `bin`
- [x] `oct`
- [x] `hex`
- [~] `open`: VFS-backed text/binary basics, CPython-style positional/keyword forms, context-manager methods, file iteration, file attribute probes, encoding/error keyword basics, and universal/newline translation foundation; exact buffering/opener/error-class semantics pending
- [x] `getattr`
- [x] `setattr`
- [x] `hasattr`
- [x] `dir`: module/class/instance basics
- [x] `vars`: module/class/instance snapshots, including slot-backed instance fields
- [~] `globals`: active live module mapping with subscript get/set/delete, membership, iteration, common dict-style methods, and live `function.__globals__`/frame `f_globals`; exact CPython `dict` identity/type semantics pending
- [x] `locals`: active frame snapshot plus module-level namespace snapshot
- [x] `eval`: string/code-object expression basics using current globals
- [x] `exec`: string/code-object statement basics using current globals
- [x] `compile`: `exec`/`eval`/`single` code-object basics
- [x] `callable`

### Standard Modules Foundation

Native or runtime-backed foundation:

- [~] `sys`: `modules`, `exc_info`, stdio objects, argv/path/import-cache containers, version/platform fields,
  default/filesystem encoding helpers, recursion-limit helpers, `intern`, `getsizeof`, trace/debug hooks, and frame placeholders; full CPython startup flags/config/runtime internals pending
- [~] `time`: `time`, `time_ns`, `monotonic`, `monotonic_ns`, `perf_counter`, `perf_counter_ns`, `process_time`,
  `process_time_ns`, `thread_time`, `thread_time_ns`, `get_clock_info`, `sleep`, and `mktime` placeholder; full calendrical tuple APIs and platform clock exactness pending
- [x] `_thread` subset
- [~] `abc` / `_abc`: native `ABCMeta`/`ABC`, `abstractmethod` markers, cache-token/register/dump/reset helpers,
  virtual subclass checks, and `isinstance`/`issubclass` metaclass hook dispatch; negative caches,
  `__subclasshook__`, and exact CPython invalidation internals pending
- [~] `atexit`: native callback registry with `register`, `unregister`, `_run_exitfuncs`; keyword args and full shutdown reporting pending
- [~] `nt` / `posix`: alias to the native `os` module foundation on the host platform
- [~] `_stat`: stat tuple indexes, common file mode constants, permission bits, and `S_IS*` helpers
- [~] `_imp`: import-lock stubs, `is_builtin`, `is_frozen`, `get_magic`, `extension_suffixes`
- [~] `_io`: module exposes VFS-backed `open`, `open_code`, `StringIO`, `BytesIO`, file-like context/read/write/seek helpers, iterator hooks, and text newline/encoding basics; concrete CPython IO type hierarchy pending
- [~] `_socket`: constants and socket object lifecycle facade; native networking pending
- [~] `_signal`: signal constants, stateful `signal`/`getsignal`, `raise_signal`, `valid_signals`, `strsignal`, and `default_int_handler` foundations; real OS signal delivery semantics pending
- [~] `select`: `select()` shape for non-network readiness lists; native descriptor polling pending
- [~] `_weakref`: `ref`, `proxy`, `ReferenceType`, `ProxyType`, `getweakrefcount`, `getweakrefs` facade; true weak lifetime/callback semantics pending
- [~] `_collections`: native `deque` foundation with common mutating methods; iteration/full CPython semantics pending
- [~] `_queue`: native `SimpleQueue` foundation with put/get/qsize/empty and catchable empty errors; blocking semantics pending

High-level modules currently backed by native/runtime code:

- [~] `threading`
- [~] `os`: VFS-backed `listdir`, exported/reused `scandir`/`DirEntry` foundation, `makedirs`, `remove`/`unlink`, `stat`, `getcwd`, `chdir`, plus `getenv`/`fspath` basics; full stat/scandir/path-like/error semantics pending
- [~] `os.path` / `ntpath` / `posixpath`: path string helpers foundation with VFS-backed `exists`/`isdir`/`isfile`/absolute resolution plus `relpath`, `samefile`, `commonprefix`, and `expandvars`; full path normalization/platform semantics pending
- [~] `stat`: stat tuple indexes, common constants, permissions bits, and file-type helper functions
- [~] `argparse`: `ArgumentParser` supports `add_argument`, option aliases, positional args, defaults, `type=int`, `store_true`, and `parse_args(list)` basics; full CPython parser/error/help behavior pending
- [~] `ast`: public `_ast`/`ast` class surface, constructible keyword/positional AST nodes with `_fields`, `dump`, `iter_fields`, `walk`, `literal_eval` for literal nodes, and parse-result shell foundations; real parser-to-AST lowering and exact CPython node metadata pending
- [~] `code`: `compile_command` uses the XLang3 compiler for complete source and returns `None` for common incomplete REPL blocks; full interactive compiler/console semantics pending
- [~] `codecs`: alias-normalized `lookup`, `getencoder`/`getdecoder`, CodecInfo encode/decode callables,
  UTF-8/UTF-8-SIG/ASCII/Latin-1 encode/decode with strict/ignore/replace/backslashreplace basics, hex
  encode/decode, and error-handler lookup/registration foundation; full codec registry/error handling pending
- [~] `contextlib`: generator `contextmanager`, `nullcontext`, `closing`, and `suppress` basics work with with-statements; async helpers and full generator exception propagation semantics pending
- [~] `ctypes`:
  Scalar classes, `.value`, pointer/byref/cast contents, `addressof`,
  `memmove`/`memset` no-op shape, string buffers, simple `Structure` field
  defaults, selected `wintypes`, `windll.kernel32` facade, and catchable
  `WinError` foundation covered. Real ABI/FFI calls, layout/alignment, arrays,
  callbacks, pointer arithmetic, and platform library loading remain pending.
- [~] `dataclasses`: annotated-field decorator generates `__init__`, `__repr__`, `__eq__`, `__dataclass_fields__`, `fields`, `is_dataclass`, `asdict`, and simple `field(default=...)` handling; frozen/order/slots/default_factory/inheritance and full CPython field semantics pending
- [~] `dis`: code-object-backed `findlinestarts`, `Bytecode`, and `get_instructions` foundations over XLang3 IR/source metadata; exact CPython bytecode/disassembly compatibility pending
- [~] `enum`: native foundation for `Enum`, `IntEnum`, `IntFlag`, `Flag`, `StrEnum`, `auto`, and decorators; real enum metaclass/member semantics pending
- [~] `fnmatch` / `glob`: native `fnmatch`, `fnmatchcase`, `filter`, `filterfalse`, `translate`, `glob.has_magic`,
  `glob.escape`, and VFS-backed `glob`/`iglob` with recursive `**` foundation; true lazy `iglob`,
  `root_dir`/`dir_fd`/`include_hidden` keyword options, bytes paths, and exact CPython path edge cases pending
- [~] `functools`: `update_wrapper`, `wraps`, `partial`, `reduce`, `cmp_to_key`, and `total_ordering` foundations; real cache wrappers, singledispatch, descriptor edge cases, and full CPython semantics pending
- [~] `__future__`: feature names and `_Feature` metadata/method basics; compiler integration is parser/runtime-owned
- [~] `getpass`: `getuser` uses host environment lookup and password readers accept CPython-shaped arguments; real terminal echo control pending
- [~] `itertools`: finite foundations for `count`, `islice`, `takewhile`, `dropwhile`, `filterfalse`, `compress`, `repeat(times)`, `chain`, `batched`, `product`, `combinations`, `combinations_with_replacement`, `permutations`, `accumulate`, `starmap`, and `zip_longest`; lazy object identity, keyword-only options, and full iterator algebra pending
- [~] `json`: native `loads`/`load`/`dumps`/`dump`, file-like I/O, CPython-style default separators,
  `indent`, `sort_keys`, `ensure_ascii`, `separators`, `skipkeys`, `default`, `object_hook`,
  `object_pairs_hook`, `parse_int`, `parse_float`, `JSONEncoder.encode`/`iterencode`, and
  `JSONDecoder.decode` foundations; exact `JSONDecodeError` payloads, `allow_nan`/`parse_constant`,
  streaming encoder details, and full CPython package behavior pending
- [~] `locale`: category constants, set/get locale, encoding helpers, normalize, and localeconv shape; real platform locale semantics pending
- [~] `marshal`: XLang3-native `dumps`/`loads` and file `dump`/`load` round-trip foundations for scalars,
  strings/bytes, and common containers; this is intentionally not CPython `.pyc`/code-object marshal exact yet
- [~] `numbers`: numeric ABC facade; real ABC registration/virtual subclass integration pending
- [~] `opcode`: public opcode map/name foundation and `_opcode` helper facade; full CPython opcode table/disassembly metadata pending
- [~] `operator`: arithmetic, in-place aliases, bitwise, comparison, truth/identity/contains, item mutation, length/count/index helpers, magic-method item fallback, and attr/item/method getter foundations; full CPython edge cases pending
- [~] `pickle`: public `pickle` and `_pickle` expose protocol constants, exceptions/classes,
  `Pickler`/`Unpickler`, and `dumps`/`loads`/file `dump`/`load`; new output uses a CPython-readable
  pickle opcode subset for common scalars/bytes/strings/containers, and XLang3 reads the same protocol-4
  subset from CPython; reducers, persistent IDs, shared-reference memo semantics, custom object state,
  extension registry, and protocol-5 out-of-band buffers pending
- [~] `platform`: platform/python version helpers foundation
- [~] `pkgutil`: VFS/import-root `iter_modules`, `walk_packages`, `extend_path`, `get_data`,
  `resolve_name`, and loader placeholder foundations; named `ModuleInfo`, full finder/loader semantics,
  zip/resource edge cases, and exact import-package behavior pending
- [~] `re`: regex compile/match/search/fullmatch, compiled `Pattern` methods, `Match.group/groups/span/start/end`, `findall`, `split`, `sub`, and `escape` facade; full CPython regex semantics pending
- [~] `signal`: public signal facade with constants, stateful handler registration, synchronous `raise_signal`, `valid_signals`, `strsignal`, and catchable `KeyboardInterrupt` from `default_int_handler`; real OS delivery/thread semantics pending
- [~] `site`: site-package path helpers, public path constants, `addsitedir`, and `addsitepackages` foundations; `.pth` processing/startup-site behavior pending
- [~] `socket`: facade over `_socket` constants and socket object basics; connect/bind/send/recv pending
- [~] `queue`: native `Queue`, `LifoQueue`, `PriorityQueue`, `SimpleQueue`, `Empty`, and `Full` foundations with ordering/maxsize/task helpers; blocking/wakeup semantics pending
- [~] `string`: public constants, `_string.formatter_parser`/`formatter_field_name_split`, and native
  `Formatter` methods for `format`, `vformat`, `parse`, `get_value`, `format_field`, and `convert_field`;
  full nested-field parsing, keyword formatting, subclass override hooks, and exact CPython formatter behavior pending
- [~] `struct`: native `calcsize`, `pack`, `pack_into`, `unpack`, `unpack_from`, `iter_unpack`,
  `Struct`, and catchable `struct.error` foundations for common endian prefixes plus integer,
  bool, float/double, char, bytes, pascal-string, and pad format units; native alignment,
  exact range diagnostics, keyword forms, true iterator object identity, and full CPython format edge cases pending
- [~] `subprocess`: constants, `Popen` wait/poll/terminate basics, `run()` with Windows child launch, `capture_output`/`stdout=PIPE` text/bytes capture, `CompletedProcess`, and catchable `CalledProcessError` foundations; POSIX process launch, async pipe draining, timeout, input, shell details, and full lifecycle semantics pending
- [~] `sysconfig`: path names/dicts, platform/version, scheme name/default/preferred helpers,
  `is_python_build`, and common config-var helpers; full install scheme compatibility pending
- [~] `typing`: common aliases, identity decorators, `TypeVar`, `NewType`, `Generic`, and `Protocol` foundations; parsed type-parameter bounds/defaults/variance/lazy evaluation and full typing runtime behavior pending
- [~] `traceback`: `format_exception`, `format_exception_only`, `format_exc`, `print_exception` basics; exact frame/line formatting pending
- [~] `linecache`: VFS-backed `getline`, `getlines`, `updatecache`, `clearcache`, `checkcache`, and `lazycache` foundation; encoding-cookie handling and exact cache invalidation semantics pending
- [~] `inspect`: common predicates, `currentframe`/`stack` placeholders, `getfile`/`getabsfile`,
  `getmodule`/`getmodulename`, `getmro`, doc cleanup, unwrap, generator/coroutine state helpers,
  Python-callable `getmembers` predicates, `getfullargspec`, and `signature`/`Signature`/`Parameter`/`BoundArguments`
  foundations for Python functions; exact frame stack, source block slicing, keyword binding, annotations,
  descriptor classification, and full CPython signature semantics pending
- [~] `runpy`: `run_module` and `run_path` basics returning globals dict snapshots
- [~] `importlib`: `import_module`, `invalidate_caches`, `importlib.util.find_spec`/`resolve_name`,
  loader/spec/module creation foundations, and VFS-backed `importlib.resources` read helpers
- [~] `types`: `ModuleType`, `SimpleNamespace`, `MethodType` basics; exact CPython type objects pending
- [~] `collections`: native `deque`, `defaultdict`, `OrderedDict`, `namedtuple`, dict-backed `Counter`, and `ChainMap` foundations; full CPython collection semantics pending
- [~] `weakref`: facade over `_weakref` basics plus `finalize` placeholder; true weak lifetime/callback semantics pending
- [~] `logging`: native logger facade with levels, `basicConfig`, root functions, `getLogger`, level-name helpers, Logger methods/effective-level checks, and no-op Handler/StreamHandler/NullHandler/Formatter classes; real handler/formatter hierarchy pending
- [~] `pathlib`: `Path`/`PurePath` facade with VFS-backed exists/read/write checks, CPython-style `name`/`stem`/`suffix`/`suffixes`/`parts`/`parent` properties, text/binary read/write, `with_name`, `with_suffix`, `/` join via native `__truediv__`, and basic path transforms; full pathlib glob/match/resolve/operator edge semantics pending
- [~] `urllib.parse`: quote/unquote helpers plus `urlparse`/`urlsplit` result objects, `urlunparse`/`urlunsplit`,
  `urljoin`, `parse_qs`, `parse_qsl`, and `urlencode` foundations; bytes handling, keyword options,
  strict parsing/errors, complete RFC edge cases, and exact CPython result tuple subclasses pending
- [~] `warnings` / `_warnings`: `warn`, `warn_explicit`, `simplefilter`, `filterwarnings`, `resetwarnings`, and `catch_warnings(record=True)` recording basics; filter/category/showwarning semantics pending
- [~] `winreg`: common HKEY/KEY/REG constants and close-key no-op; real registry operations pending
- [~] `zlib`: native zlib-backed `compress`, `decompress`, `compressobj`, `decompressobj`, `crc32`, `adler32`, common constants, and stream object state basics; dictionaries, copy, checksums/compression edge cases, and exact CPython error semantics pending
- [~] `zipfile`: native `ZipFile`/`PyZipFile`/`ZipInfo`/`ZipExtFile`/`Path` facade with stored/deflated archive `is_zipfile`, `namelist`, `infolist`, `getinfo`, `read`, `open` read/write handles, `write`, `writestr`, `mkdir`, `extract`, `extractall`, `testzip`, `close`, context managers, comments, common `ZipInfo` metadata, file-like archive objects, `setpassword`, `ZipExtFile` seek/tell/readline/readlines/state helpers, CPython-style `Path` properties, simple glob/rglob/match/relative checks, `ZipInfo.from_file`, `ZipInfo._for_archive`, `ZipInfo.FileHeader`, `PyZipFile.writepy` member naming, ZIP limit/compression constants, exclusive create, extraction sanitization, and keyword argument basics; encrypted ZIP, true ZIP64 large-file archives, optional BZIP2/LZMA/Zstandard payload engines, full `zipfile.Path` edge semantics, real `PyZipFile.writepy` bytecode compilation intentionally skipped for XLang3 IR, and exact CPython edge cases pending
- [~] `xmlrpc` / `http`: package/module import foundation plus `xmlrpc.client.dumps`/`loads` scalar round-trips, common XML-RPC classes, `http.HTTPStatus`, `http.client` constants/responses/classes, and `http.server` class names; real HTTP/XML-RPC networking and complete protocol behavior pending

### Async, Tasks, And Threads

- [x] native `task` module
- [x] minimal `asyncio` facade
- [x] `async def` syntax accepted
- [x] `await` syntax accepted and lowered to IR
- [~] `Await` IR operation
- [~] real resumable coroutine frames: `async def` now returns coroutine-marked generator-backed VM frames, direct calls are lazy, `await`/`asyncio.run` drive coroutine frames to completion, and coroutine `__await__` is exposed; full scheduler-yielding and CPython coroutine state APIs pending
- [~] event loop semantics: thread-local event loop facade with `new_event_loop`, `get_event_loop`, `set_event_loop`, `get_running_loop`, `run_until_complete`, `create_task`, `close`, and `is_closed`; real selector/scheduler policy pending
- [~] `asyncio` compatibility: `run`, `create_task`, `gather`, `sleep`, and loop facade basics covered; CPython task cancellation, futures, transports, and scheduler semantics pending
- [x] `_thread` subset
- [x] `threading.Thread` subset
- [x] `threading.Lock` subset
- [~] Python-compatible thread lifecycle details: `Thread` exposes `name`, `daemon`, `ident`, `native_id`, `_is_stopped`, start-once checks, `join(timeout)`, `main_thread`, `enumerate`, and live-worker-aware `active_count`; full CPython shutdown/daemon/current-thread object identity semantics pending
- [~] thread-local trace hooks: `sys.settrace()` is stored per runtime/native thread and `threading.settrace()` is copied into new `threading.Thread`/`_thread` workers; full profile-hook and edge-case parity pending
- [x] no-GIL data sharing policy finalized in `doc/no-gil-runtime-policy.md`; mutable-container/native-module enforcement audits remain tracked by their implementation rows

### Filesystem And IO

- [x] runtime VFS abstraction
- [~] file object: read/write/close/context manager plus read(size), readline(s), writelines, seek/tell/truncate, `name`/`mode`/`closed`/`encoding`/`errors`/`newlines` attributes, readable/writable/seekable/isatty/fileno probes, iterator protocol, newline translation basics, and text encoding/error basics; exact buffering/error-class semantics pending
- [x] host filesystem backend
- [x] Pico flash file store foundation
- [~] CPython-compatible `open`: VFS path/path-like input, `r/w/a/x/+` mode parsing, text/binary positional and keyword handling, and file iterator behavior; full error classes/opener semantics pending
- [~] text/binary modes: text strings and binary bytes/bytearray for core read/write paths
- [~] buffering behavior: `buffering` keyword is accepted and validated; buffering policy is still VFS-buffer based
- [~] encoding behavior: UTF-8/UTF-8-SIG/ASCII/Latin-1 text paths use `encoding`/`errors`/`newline` keywords with basic codec conversion and newline translation; full codec registry matrix pending
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

- [~] `enum` audit:
  Native module now turns enum subclass constants into member objects and tests
  member creation, value lookup, aliases, `auto()`, `IntEnum` basics, class
  attributes, iteration over canonical members, member string display, and
  `unique` duplicate rejection. Remaining work: CPython-exact metaclass
  behavior, `repr`, `Flag`/`IntFlag` operators, richer decorators, and
  pickling-facing helpers.

- [~] inherited builtin constructor audit:
  subclasses of `int`, `str`, `float`, and `bytes` route through builtin
  constructors and preserve class-level constants, but currently return base
  scalar/bytes values rather than subclass instances; CPython-compatible boxed
  scalar subclass identity/arithmetic/string behavior remains pending.

- [~] `os.scandir` / `DirEntry` audit:
  exported `os.DirEntry`, reused DirEntry class, entry `name`/`path`,
  `is_file`, `is_dir`, and `stat().st_size` fixture coverage added;
  context-manager iterator behavior and symlink/follow semantics pending
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

- [~] tokenizer/string-literal audit:
  Section fixture covers raw strings, bytes escapes, f-strings, adjacent
  literals, triple strings after expressions, comments, escaped quotes, and
  triple quote sequences inside normal strings. Remaining work: broader
  tokenizer parity against CPython `Lib/tokenize.py`.

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
