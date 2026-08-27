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

## Current Progress Snapshot

Last updated after the native `sys._is_gil_enabled` no-argument validation batch.

Current checklist count:

- Syntax compatibility: 146 checked, 0 partial, 0 missing.
- Runtime compatibility: 144 checked, 111 partial, 0 missing.
- Recent compatibility debt: 0 checked, 5 partial, 0 missing.

What this means:

- The parser/syntax surface tracked in this audit is currently covered by section fixtures.
- Runtime compatibility has broad tested foundations, but many modules remain partial because exact CPython edge behavior is intentionally still tracked.
- A partial item is not a stub: it must have a declared supported subset and tests.
- A checked item means implemented enough for the current audit scope and fixtures, not a promise that every CPython implementation detail is identical.

Recent completed batches:

- Tightened native `sys._is_gil_enabled`: the probe now uses the shared
  CPython-style sys no-argument TypeError path while preserving the enabled
  boolean result, and the Standard Modules fixture covers it with the runtime
  no-argument probe matrix.
- Tightened native `sys._debugmallocstats`: allocator diagnostics now route
  through the active `sys.stderr.write` stream before returning `None`, so
  redirected stderr observes the CPython-style diagnostics path while the
  existing allocator counters remain runtime-backed.
- Expanded native `sys.setprofile` live dispatch for native/C call paths:
  native callable wrappers now emit CPython-style `c_call`, `c_return`, and
  `c_exception` events with the current Python frame captured at native-call
  entry, preserving callback-recursion suppression across the event callback.
- Expanded native `sys.setprofile` / `_setprofileallthreads` from stored hook
  metadata into live VM dispatch for Python `call`, `return`, and `exception`
  events using current frame snapshots, with recursion suppression while a
  profile callback is running; `threading.setprofile` / `getprofile` now
  preserve the hook for new `threading.Thread` workers.
- Expanded native `time` CPython 3.14 metadata: the module now publishes
  `_STRUCT_TM_ITEMS == 11`, matching the extended `struct_time` field count
  and the existing tuple-backed `struct_time` type metadata.
- Expanded live native `sys.monitoring` PEP 669 coverage for generators: VM
  generator suspension now emits `PY_YIELD` with the yielded value, generator
  continuation now emits `PY_RESUME`, and generator completion now preserves
  the return value on `StopIteration.value`/`args` for `next()` and generator
  method paths.
- Expanded live native `sys.monitoring` PEP 669 coverage: VM control-flow
  execution now emits global and code-local `JUMP`, `BRANCH_LEFT`, and
  `BRANCH_RIGHT` callbacks with CPython-style `(code, instruction_offset,
  destination_offset)` arguments for unconditional jumps and conditional
  branch outcomes.
- Tightened native `time.asctime` / `time.ctime`: both now use a
  CPython-style C-locale asctime formatter with space-padded single-digit
  month days, while preserving two-digit days, and the Standard Modules
  fixture covers both forms.
- Expanded live native `sys.monitoring` PEP 669 coverage: native callable
  dispatch now emits `CALL` plus the `C_RETURN`/`C_RAISE` companion callbacks
  controlled by the active `CALL` mask, with callback-recursion suppression
  preserved and fixture coverage for successful and failing native calls.
- Tightened native `sys.monitoring` event-set validation: global and local
  event masks now reject any `C_RETURN`/`C_RAISE` request, including the paired
  mask, matching CPython 3.14's static validation.
- Expanded native `sys` CPython 3.14 startup metadata: `sys.__doc__` is now
  published as real module documentation, and `sys.__interactivehook__` plus
  `sys._baserepl` are exposed as callable no-op hooks with catchable
  no-argument arity validation.
- Expanded native `sys._jit` CPython 3.14 metadata: the runtime JIT probe
  module now exposes the CPython module docstring alongside the existing
  inactive/unavailable state probes, with Standard Modules fixture coverage.
- Tightened native `sys.breakpointhook` / `sys.__breakpointhook__`: both
  aliases now accept keyword calls through the native keyword-call path, so
  the `PYTHONBREAKPOINT=0` no-op compatibility path used by tooling handles
  positional and keyword arguments without rejecting the call shape.
- Tightened native `time.strptime`: C-locale `%R`, `%T`, and `%r`
  composite time directives now parse through the runtime parser instead of
  platform fallback, including CPython-style 12-hour `%r` AM/PM validation and
  whitespace rejection for malformed inputs such as `13:02:03 PM` and
  missing-space meridiem forms.
- Tightened native `time.strptime`: `%Z` now accepts the platform timezone
  names published through `time.tzname`, preserves the matched `tm_zone`, sets
  CPython-style `tm_isdst` for standard/daylight names, and leaves
  `tm_gmtoff` unset for timezone-name-only parses.
- Tightened native `time.strptime`: parsed calendar dates now reject impossible
  month/day combinations such as February 31, April 31, and explicit
  non-leap-year February 29 with catchable `ValueError`, while preserving
  CPython 3.14's accepted default-year February 29 behavior.
- Tightened native `time.strptime`: `%j` ordinal-day parsing now validates the
  CPython 1..366 range and normalizes day 366 in a common year to January 1 of
  the following year while preserving the returned `tm_yday` slot.
- Expanded native `time.strptime`: C-locale composite directives `%c`, `%x`,
  and `%X`, plus space-padded day `%e`, now parse into CPython-style
  normalized `struct_time` fields for covered formats, with missing `%c` year
  and invalid `%e` day failures remaining catchable `ValueError`.
- Tightened native `time.strptime`: ISO week parsing now accepts CPython-style
  `%G` ISO years with `%V` ISO week numbers and weekday directives, derives
  the corresponding calendar date/year-day fields, and rejects invalid week 53
  or incompatible calendar-year combinations with catchable `ValueError`.
- Tightened native `sys.getsizeof`: callable `__sizeof__()` results of
  `True`/`False` are now accepted with bool-as-int semantics instead of
  falling through to default handling or raising `TypeError`.
- Tightened native `time.strptime`: `%f` now accepts CPython-style one- to
  six-digit fractional seconds and discards the fraction while preserving the
  normalized `struct_time` fields; missing or trailing fractional input remains
  a catchable `ValueError`.
- Tightened native `time.strptime`: `%U`/`%W` week numbers now combine with
  `%w`/`%u` weekday directives to derive CPython-style month/day, weekday, and
  year-day fields for Sunday-first, Monday-first, and ISO weekday inputs.
- Tightened native `time.strptime`: `%y` now uses CPython's 1969/2068
  two-digit-year pivot, `%I`/`%p` parse 12-hour clocks with AM/PM
  normalization, and these formats use the runtime parser's CPython-style
  weekday/year-day derivation instead of platform `std::get_time` leftovers.
- Tightened native `time.strptime`: ordinal-day `%j` parsing now derives
  CPython-style month/day/year-day fields, `%z` preserves parsed UTC offsets
  in `tm_gmtoff`, and `%Z` recognizes UTC/GMT-style zone names with
  CPython-style `tm_zone`/`tm_isdst` metadata.
- Tightened native `sys` CPython 3.14 frame/probe parity:
  `sys._getframe()` now treats negative depths as the current frame while
  accepting bool depths with normal `int` semantics, and
  still raising catchable `ValueError` for too-shallow positive depths,
  `sys._getframemodulename()` returns the active module name for current,
  caller, negative-depth, and bool-depth probes and returns `None` when the
  requested frame is unavailable, and `sys._is_gil_enabled()` argument errors
  are now catchable `TypeError`.
- Tightened native `time.strptime`: omitted date/time fields now use
  CPython-style defaults before normalization, weekday/year-day fields are
  populated consistently for partial formats, parse failures raise catchable
  `ValueError`, and trailing unconverted input is rejected.
- Expanded native structseq metadata parity: `sys.version_info`, `sys.flags`,
  `sys.int_info`, `sys.float_info`, `sys.hash_info`, `sys.thread_info`,
  Windows `sys.getwindowsversion()`, `sys.implementation.version`, and
  `sys.get_asyncgen_hooks()` result types now expose CPython-style
  `__match_args__`, with `asyncgen_hooks.__module__` matching CPython's
  `builtins`; `time.struct_time` now exposes `__match_args__` and type-level
  member descriptors for the nine sequence fields.
- Tightened native `_abc` cache parity: virtual registry matches no longer
  enter the positive subclass cache, and `_reset_registry()` now preserves
  positive/negative caches plus the cache token like CPython 3.14.
- Expanded native `_abc._get_dump()` to expose CPython-style snapshot sets of
  callable weak references for the registry, positive cache, and negative cache
  while keeping internal ABC matching state identity-based.
- Expanded native `sys` CPython 3.14 metadata/probe coverage with `_git`,
  `_vpath`, `_home`, `float_repr_style`, `getunicodeinternedsize()`,
  `_get_cpu_count_config()`, `is_remote_debug_enabled()`,
  `_clear_type_descriptors()`, `_dump_tracelets()`, `_settraceallthreads()`,
  and `_setprofileallthreads()`.
- Expanded native `sys` structseq behavior: `version_info`, `flags`,
  `int_info`, `float_info`, `hash_info`, `thread_info`, Windows
  `windows_version`, `sys.implementation.version`, and `asyncgen_hooks` now
  use the runtime tuple base and expose tuple-backed `count()` / `index()`
  over their sequence fields while retaining CPython-style named metadata.
- Expanded native `sys` trace/debug hook coverage with `sys.call_tracing`.
- Expanded process-published `sys` startup metadata so `sys.executable`,
  `sys._base_executable`, `sys.prefix`, `sys.base_prefix`,
  `sys.exec_prefix`, `sys.base_exec_prefix`, and `sys.orig_argv` stay
  synchronized after launcher initialization; normal startup now matches
  CPython 3.14 by leaving legacy virtualenv-only `sys.real_prefix` absent.
  The function now validates the CPython-style `(func, args_tuple)` shape,
  calls through the runtime's normal callable dispatch with tuple unpacking,
  and restores the active trace hook after the call.
- Expanded native `abc`/`_abc`: `__abstractmethods__` is now represented by
  a runtime `frozenset` object for ABCMeta-created classes, direct
  `_abc_init()` classes, and `abc.update_abstractmethods()` recomputation.
  The `frozenset` builtin now remains a class for `isinstance(...,
  frozenset)` probes while using a VM-backed constructor that returns immutable
  set storage.
- Expanded native `sys` async-generator hook configuration: added
  stateful `get_asyncgen_hooks()` / `set_asyncgen_hooks()` with positional
  and keyword updates, callable-or-`None` validation, and a structseq-like
  `asyncgen_hooks` result exposing named fields and sequence slots for
  CPython-style stdlib runtime probes.
- Expanded native frame snapshots behind `sys._getframe()` and
  `sys._current_frames()`: frame objects now expose `f_builtins` as a
  snapshot of the active builtins module mapping instead of an empty
  placeholder dict, covering stdlib inspection/debug probes that expect
  builtins such as `len`, `print`, and exception classes to be present.
- Expanded native `sys` int-string digit runtime state: `sys.int_info` now
  exposes CPython 3.14 `default_max_str_digits` and
  `str_digits_check_threshold`, while `get_int_max_str_digits()` and
  `set_int_max_str_digits()` preserve the configured limit, validate the
  CPython 640-or-0 rule, and keep `sys.flags.int_max_str_digits` plus its
  sequence slot in sync.
- Expanded native `sys.stdlib_module_names` to the full Python 3.14 top-level
  standard-library module-name set, so stdlib/package probes can recognize
  importable standard modules such as `asyncio`, `email`, `encodings`, and
  `tomllib` without pure-Python facades.
- Added native `sys._is_immortal` for CPython 3.14 feature probes. XLang3
  reports non-refcounted tagged singleton/scalar values as immortal and heap
  objects as non-immortal, matching the runtime's actual ownership model
  without adding a pure-Python facade.
- Expanded native `sys` structseq-like metadata: generated `version_info`,
  `flags`, `float_info`, `hash_info`, `thread_info`, and Windows
  `windows_version` type objects now expose CPython-style
  `n_sequence_fields`/`n_fields`/`n_unnamed_fields` metadata plus named member
  descriptors, so stdlib probes that inspect `type(sys.version_info)` and
  related runtime config objects work without pure-Python facades.
- Expanded native `time.struct_time`: the type object now exposes
  CPython-style structseq metadata (`n_sequence_fields`, `n_fields`, and
  `n_unnamed_fields`) plus non-null `tm_zone`/`tm_gmtoff` member descriptors,
  matching stdlib probes that inspect the type rather than an instance.
- Expanded native `sys.getsizeof`: the function now honors a callable
  `__sizeof__()` protocol result for Python objects and returns the supplied
  default when an unusable non-callable `__sizeof__` attribute is present,
  while preserving the existing XLang3 shallow-size fallback for objects
  without the protocol.
- Expanded native `sys.flags`: the structseq-like object now exposes the
  CPython 3.14 named-only `gil`, `thread_inherit_context`, and
  `context_aware_warnings` fields while preserving the 18-field sequence view
  and reporting `n_fields == 21` for stdlib feature probes.
- Expanded native `sys`: `sys.implementation` now exposes
  `supports_isolated_interpreters`, stack-trampoline probes report the
  currently inactive/unavailable XLang3 runtime state, and `sys._jit` exposes
  CPython 3.14-style JIT state probes for stdlib feature detection.
- Expanded native `sys`: `_current_exceptions()` now snapshots active
  exception objects per live XLang3 thread, so Python 3.14-style
  thread-id-keyed exception introspection reports worker-thread handlers as
  well as the caller thread.
- Expanded native `sys`: `_current_frames()` now returns runtime-maintained
  frame snapshots for live XLang3 threads, including worker threads blocked in
  standard threading primitives, instead of only reporting the caller thread.
- Expanded native `sys`: `intern()` now keeps a runtime intern table and
  returns the canonical string object for repeated equal strings, with
  `_is_interned()` exposing object-identity membership for CPython-style
  feature probes.
- Expanded native `sys`: added CPython-style startup/config probe surface for
  `_stdlib_dir`, `_framework`, Windows `winver`/`dllhandle`,
  `getwindowsversion()`, `_enablelegacywindowsfsencoding()` with observable
  filesystem encoding/error state updates, and `_debugmallocstats()` backed by
  XLang3 allocator counters so stdlib feature detection can run without
  compatibility facades.
- Expanded native `time.struct_time`: the constructor now accepts the CPython
  optional dict form for `tm_zone` and `tm_gmtoff`, preserves those named
  fields, rejects duplicate/unexpected extra field names, and rejects sequences
  longer than 11 fields with `TypeError`.
- Expanded native `time.struct_time`: instances now expose a CPython-style
  `__repr__()` that renders the nine sequence fields with named structseq
  labels while omitting the non-sequence `tm_zone`/`tm_gmtoff` extras, matching
  CPython 3.14 constructor and timestamp results.
- Expanded native `sys`: `set_coroutine_origin_tracking_depth()` now stores
  and reports the thread-local configured depth through
  `get_coroutine_origin_tracking_depth()`, including negative-depth validation.
- Expanded native `sys`: added `getrefcount()` backed by XLang3 object
  refcounts and `getallocatedblocks()` backed by current thread allocator
  block counters for CPython-style runtime/memory probes.
- Expanded native `sys`: added `stdlib_module_names` metadata for standard
  library membership probes, and keyed `_current_frames()` /
  `_current_exceptions()` by the active XLang3 thread identifier instead of a
  fixed placeholder id.
- Expanded native `sys`: `_current_frames()` now reports the active runtime
  frame for the current thread, `_current_exceptions()` follows the Python 3.14
  single-exception-value shape, and cache-clear/coroutine-origin tracking
  helpers cover common stdlib cleanup/introspection calls.
- Expanded native `abc`/`_abc`: `ABCMeta`-created classes now expose
  computed `__abstractmethods__`, inherited abstract methods are cleared by
  concrete overrides after base/metaclass reconciliation, and abstract ABC
  instantiation raises `TypeError`.
- Expanded native `sys`: stdio objects now expose common capability probes
  (`isatty`, `readable`, `writable`, `seekable`, `fileno`, `closed`, and
  `line_buffering`), and `sys.addaudithook`/`sys.audit` maintain and dispatch
  Python-level audit hooks.
- Expanded native `abc` abstract descriptor decorators: `abstractclassmethod`,
  `abstractstaticmethod`, and `abstractproperty` now create descriptor objects
  while preserving observable `__isabstractmethod__` metadata.
- Expanded native `_abc` registration parity: direct real subclasses are
  treated as already registered without cache-token churn, while virtual
  registrations that would create inheritance cycles raise `RuntimeError`.
- Expanded native `_abc` cache state: positive subclass cache, negative subclass
  cache, `_get_dump` cache visibility, `_reset_caches`, and negative-cache
  invalidation after virtual subclass registration.
- Expanded native `_abc_init`: direct `_abc._abc_init(cls)` now validates class
  input, computes `__abstractmethods__`, initializes registry/cache state, and
  enables abstract-instantiation enforcement for classes initialized through the
  CPython `abc.py` path.
- Expanded native `sys`: `displayhook()` now writes through the active
  `sys.stdout`, ignores `None`, and maintains `builtins._`; `excepthook()` now
  writes a useful exception summary through the active `sys.stderr` instead of
  acting as a silent placeholder.
- Expanded native `abc`: `update_abstractmethods()` now recomputes
  `__abstractmethods__` after post-creation class mutations, returns non-ABC
  objects unchanged, and feeds the existing abstract-instantiation enforcement.
- Expanded native `time` timestamp conversion: `gmtime`, `localtime`, and
  `ctime` now accept bool timestamp arguments with CPython-compatible
  `False`/`True` conversion to epoch seconds `0`/`1`.
- Fixed `isinstance`/`issubclass` ABC dispatch ordering so `ABCMeta` hooks run
  before direct subclass acceptance, including `__subclasshook__` returning
  `False` for a real subclass.
- Expanded native `time` timezone metadata to use platform C-runtime `timezone`,
  `altzone`, `daylight`, and `tzname` values instead of synthesized UTC defaults.
- Added shared Python source-encoding detection/decoding for UTF-8, UTF-8 BOM,
  ASCII, and Latin-1 coding cookies; wired it into `linecache` and `_tokenize`.
- Fixed bytes-regex match groups so CPython `Lib/tokenize.py` can run its
  `detect_encoding()` path against bytes input.
- `eacf098` expanded `time`: constructible/indexable/iterable `struct_time`, `strptime`, and timezone constants.
- `9c7a42b` expanded `enum`: direct enum `__repr__`/`__str__`, `Flag`/`IntFlag` bitwise operators, inversion, and composite names.
- `148a008` expanded callable metadata: bound methods, raw `staticmethod`/`classmethod`, native function metadata, and class `__dict__` inspection.
- `fec3484` expanded `_tokenize`: native COMMENT/NL preservation and Python 3.14 token-number alignment.

Recommended next batch:

- `inspect` source/signature gaps that depend on improved source lookup.
- Then `traceback` frame/line formatting.
- Then importlib/resource edge behavior for real stdlib package loading.

## Standard Module Closeout Policy

The standard-module section is now treated as a closeout queue, not a loose
progress list. A `[~]` item is allowed only when the implemented subset is real,
fixture-backed, and the remaining CPython gap is named. If the remaining gap can
break ordinary Python code, it must stay in the active closeout queue until it is
implemented or moved to an explicitly documented lower-risk edge bucket.

Closeout meanings:

- `P0 blocker`: likely to break pure-Python stdlib, debugpy, import tooling, or
  ordinary file/source lookup. Fix before returning to IDE/debugpy compatibility.
- `P1 common stdlib risk`: likely to break normal Python application code, but
  not the next debugger/import milestone. Fix during the standard-module pass.
- `P2 edge parity`: exact CPython/platform/internal behavior. Keep tracked with
  tests where possible, but do not claim full parity until the edge behavior is
  implemented.

Current P0 closeout queue:

- `linecache`: exact source cache invalidation.
- `tokenize` / `_tokenize`: exact token text and broader CPython tokenizer parity.
- `inspect`: real source lookup, frame stack, signatures, annotations, and descriptor classification.
- `traceback`: exact frame, line, exception-chain, and formatting behavior.
- `importlib` / `pkgutil`: real finder/loader/resource/package semantics.
- `sys` / `os` / `_io` / `open`: startup paths, stdio/file objects, path-like objects, and VFS-backed file semantics.

Current P1 closeout queue:

- `argparse`, `contextlib`, `functools`, `collections`, `itertools`, `pathlib`,
  `subprocess`, `json`, `pickle`, `queue`, `threading`, `typing`, `re`,
  `codecs`, `locale`, `struct`, `zlib`, `zipfile`, `urllib.parse`, `warnings`,
  `logging`, `dataclasses`, `enum`, `abc`, `socket`, and `select`.

Current P2 parity queue:

- `.pyc`/CPython bytecode and exact `marshal` code-object format.
- Full CPython import locks, frozen-module internals, and startup flag matrix.
- True weakref lifetime/callback semantics and exact `mappingproxy`/type identity internals.
- Full `ctypes` ABI/FFI, callback, structure-layout, and platform-loader behavior.
- Real OS signal delivery/thread semantics and full socket/network descriptor behavior.
- Exact timezone/DST/locale behavior and full Unicode database/grapheme/codec edge matrix.
- Encrypted ZIP, true ZIP64 large-file archives, optional BZIP2/LZMA/Zstandard payload engines, and exact `zipfile.Path` edge semantics.

Rule:

```text
Do not hide compatibility debt by marking broad modules [x]. Each [~] row must
name the exact remaining gap. During implementation, close P0 first, then P1.
P2 items may remain partial only when their risk and CPython-specific nature are
documented here.
```

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
- [x] implicit continuation with triple-quoted string literals and chained calls inside call arguments
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
- [x] `from __future__ import annotations` stores function and class annotations as strings for tested
  forward refs, PEP 604 unions, generic aliases, string literals, and tuple-generic forms
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
- [x] string literal lexing edge cases: quote-heavy normal strings, triple-quoted strings, and triple-string suffix/chained-call tokenization covered in section fixture
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
- [x] varargs/kwargs objects, including `*args` call expansion from general iterables
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
- [x] iteration protocol completeness: `iter()`, `next()`, default exhaustion value, user-defined `__iter__`/`__next__`, constructor collection, star-argument expansion, and lazy iterator basics
- [x] iterator objects compatibility: range/sequence/dict/set/generator plus enumerate/zip/map/filter and protocol-wrapper foundations
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
- [x] module specs and source metadata: native, source, package, namespace, and zip-source modules expose real `__spec__`, `__loader__`, `origin`, `parent`, `has_location`, package search-location metadata, and source-backed `__file__` where applicable
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

- [~] `sys`: `modules`, `exc_info`, stdio objects, argv/orig_argv/path/import-cache containers,
  version/platform/prefix/executable fields including `_base_executable`,
  `exec_prefix`, `base_exec_prefix`, and CPython-normal `real_prefix` absence, structseq-like `version_info`/`flags`/`int_info`/
  `float_info`/`hash_info`/`thread_info` with instance/type field counts, type-level named member descriptors,
  tuple inheritance, sequence iteration, tuple-backed `count`/`index` including bool-as-int
  start/stop bounds and CPython-style out-of-range `IndexError`, CPython-style named-field `repr`,
  and type-level `__match_args__`,
  `implementation` metadata, `builtin_module_names`,
  CPython 3.14 top-level frozen `stdlib_module_names`, `_git`/`_vpath`/`_home`
  metadata, `float_repr_style`, `getunicodeinternedsize`,
  `_get_cpu_count_config`, `is_remote_debug_enabled`,
  `_settraceallthreads`/`_setprofileallthreads`, `_clear_type_descriptors` with
  CPython-style arity/type/immutable-type errors,
  `_dump_tracelets`, default/filesystem encoding helpers, recursion-limit helpers, `intern`
  with runtime canonicalization plus `_is_interned`, `getsizeof` with `__sizeof__`
  protocol/default handling including bool-as-int return values, TypeError default
  fallback and negative-result `ValueError`, `getrefcount`,
  `getallocatedblocks`, `exit`, display/exception hooks with stdio routing and `builtins._`,
  `breakpointhook`/`__breakpointhook__` no-op behavior including keyword-call support,
  audit hook dispatch including CPython-style call-time failure for registered non-callable hooks,
  stdio capability probes, profile/switch-interval/int-string helpers with `sys.int_info`
  and stateful `sys.flags.int_max_str_digits`, trace/debug hooks including
  `call_tracing`, live `sys.setprofile` / `_setprofileallthreads` dispatch for Python
  `call`/`return`/`exception` events with current frame arguments and callback-recursion
  suppression, native/C `c_call`/`c_return`/`c_exception` profile events with
  current-frame arguments for covered native callable paths, plus
  `threading.setprofile` inheritance for new threads,
  `implementation.supports_isolated_interpreters`, CPython-normal Windows `implementation._multiarch`
  absence while preserving non-Windows `_multiarch`, stack-trampoline probes, `sys._jit` module doc metadata and state probes,
  `sys.monitoring` import/configuration surface with CPython 3.14 tool IDs, event constants,
  tool-name reservation/freeing plus `clear_tool_id` preserving reservation/local masks while clearing global events/callbacks,
  global/local event masks, callback replacement, restart/all-events
  helpers, CPython-style `events` `types.SimpleNamespace` metadata/repr,
  C return/raise event-set validation including paired-mask rejection, inactive-tool local-event rejection,
  bool-as-int tool/event IDs, single C return/raise callback registration, and catchable validation failures,
  live global and code-local `PY_START`/`PY_RETURN`/`LINE`/`INSTRUCTION` callback dispatch with
  CPython-style callback arguments, live native `CALL` plus companion `C_RETURN`/`C_RAISE`
  dispatch for successful and failing native call paths, stable code-object local event matching,
  callback-recursion suppression, live global and code-local `JUMP`/`BRANCH_LEFT`/
  `BRANCH_RIGHT` dispatch with CPython-style destination-offset callback arguments,
  live generator `PY_YIELD`/`PY_RESUME` dispatch with CPython-style callback
  arguments and generator return propagation through `StopIteration.value`,
  and live caught-exception `RAISE`/`EXCEPTION_HANDLED`
  dispatch with CPython-style callback arguments, live `PY_UNWIND` dispatch when
  exceptions leave unhandled Python frames with CPython-style callback arguments,
  live explicit `RERAISE` dispatch for bare `raise` in active handlers with
  CPython-style callback arguments,
  `_is_immortal` for XLang3 tagged singleton/scalar values,
  live-thread-id-keyed `_current_frames` snapshots,
  live-thread-id-keyed `_current_exceptions`, cache-clear hooks, configurable coroutine-origin tracking helpers,
  async-generator hook configuration with structseq-like `asyncgen_hooks` including CPython-style
  `builtins` type-module metadata,
  catchable `TypeError` arity failures for no-argument runtime/config/frame/cache/JIT
  probes,
  `_stdlib_dir`, `_framework`, Windows `winver`/`dllhandle`, `getwindowsversion`,
  stateful `_enablelegacywindowsfsencoding`, allocator-backed `_debugmallocstats`
  with active `sys.stderr` routing,
  CPython 3.14 startup metadata/hooks including `__doc__`,
  `__interactivehook__`, `_baserepl`, and CPython-normal Windows `abiflags`
  absence while preserving non-Windows `abiflags`,
  CPython 3.14 `flags` named-only metadata for `gil`, `thread_inherit_context`,
  and `context_aware_warnings`, CPython-default `sys.dont_write_bytecode` plus
  `sys.flags.dont_write_bytecode`/`hash_randomization`/`utf8_mode` startup values,
  and frame placeholders with populated
  `f_builtins`, `_getframemodulename` with CPython-style negative-depth and
  bool-depth handling plus too-shallow-stack behavior, and CPython-default
  `_is_gil_enabled` enabled result with shared CPython-style no-argument
  `TypeError` validation;
  full CPython startup flags/config/runtime internals, remaining CPython profile edge
  cases outside the covered Python and native C call/return/exception matrix, and remaining live PEP 669
  event coverage beyond instruction/call/line/return/generator-resume-yield/caught-exception/unwind/reraise paths pending
- [~] `time`: `time`, `time_ns`, `monotonic`, `monotonic_ns`, `perf_counter`, `perf_counter_ns`, `process_time`,
  `process_time_ns`, `thread_time`, `thread_time_ns`, `get_clock_info`, `sleep`, `localtime`,
  `gmtime`, `mktime`, `strftime`, `strptime`, `asctime`/`ctime` CPython-style C-locale
  formatting including space-padded single-digit month days, constructible/indexable/iterable
  `struct_time` with CPython 3.14-style instance/type `n_fields`/`n_sequence_fields`/
  `n_unnamed_fields`, module-level `_STRUCT_TM_ITEMS`, type-level `__match_args__`,
  and named/member fields for sequence slots plus `tm_zone`/`tm_gmtoff`, constructor dict
  extra-field handling, long-sequence rejection, verbatim constructor preservation of sequence
  fields including irregular or non-int stored values, tuple-subclass identity with tuple-backed
  `count`/`index` including bool-as-int start/stop bounds, CPython-style named-field `__repr__`, platform-backed timezone constants/names,
  bool timestamp arguments for `gmtime`/`localtime`/`ctime`, and `strptime`
  CPython default-field filling, weekday/year-day normalization,
  two-digit-year `%y` pivoting, 12-hour `%I`/`%p` AM/PM normalization,
  ordinal-day `%j` including CPython-style common-year 366 overflow and override
  of explicitly parsed calendar fields,
  `%U`/`%W` week-number date derivation with `%w`/`%u`
  weekdays, ISO week `%G`/`%V` date derivation with numeric and named weekday
  directives, `%z` UTC offsets through `tm_gmtoff` including CPython-permissive
  large offset hours, UTC/GMT and platform `time.tzname` `%Z` metadata including lowercase spelling preservation,
  C-locale `%a`/`%A` weekday names, `%b`/`%h`/`%B` month names,
  `%c`/`%x`/`%X`/`%R`/`%T`/`%r` composite directives including single-digit day spacing,
  space-padded `%d`/`%e` days, and CPython 3.14 `%k`/`%l` blank-padded
  24-hour/12-hour clocks plus lowercase `%P` AM/PM handling,
  `%z` compact seconds/fractional-second offset acceptance with rejected lowercase/malformed
  offsets, `%f` fractional-second acceptance/discarding, invalid calendar-date rejection,
  CPython-style whitespace matching for format and `%c` composite whitespace runs
  including tab/run input and missing-whitespace rejection,
  catchable `ValueError` failures including CPython-style bad-directive and stray-percent rejection,
  and trailing-input rejection;
  broader locale-specific parsing and historical DST edge behavior remain pending
- [x] `_thread` subset
- [~] `abc` / `_abc`: native `ABCMeta`/`ABC`, `abstractmethod` markers and abstract descriptor decorators,
  computed `__abstractmethods__` for ABCMeta-created classes, inherited abstract-method clearing
  through concrete overrides, `abc.update_abstractmethods` recomputation after class mutation,
  `_abc_init` class initialization for the CPython `abc.py` path,
  abstract-class instantiation `TypeError`, cache-token/register/dump/reset helpers
  with catchable `TypeError` argument validation,
  virtual subclass checks, direct-subclass no-op registration, inheritance-cycle rejection,
  `isinstance`/`issubclass` metaclass hook dispatch before direct subclass acceptance,
  and ABC `__subclasshook__` True/False/`NotImplemented` fallback behavior,
  positive/negative subclass caches,
  negative-cache invalidation after virtual subclass registration including
  CPython-style stale `_get_dump` negative-cache snapshots/version metadata until the
  next subclass check, virtual registry matches staying out of the positive cache, `_reset_registry` preserving caches/token,
  and CPython-style weakref-backed `_get_dump` snapshot sets whose entries are
  callable `weakref.ReferenceType` instances; exact CPython weakref lifecycle cleanup
  remains pending
- [~] `atexit`: native callback registry with `register`, `unregister`, `_run_exitfuncs`, LIFO execution, positional args, keyword args, and callable-instance callbacks; full shutdown reporting pending
- [~] `nt` / `posix`: alias to the native `os` module foundation on the host platform
- [~] `_stat`: stat tuple indexes, common file mode constants, permission bits, callable
  `S_IFMT`/`S_IMODE`, and `S_IS*` helpers
- [~] `_imp`: import-lock state, `is_builtin`, frozen-probe helpers, registered-module
  `create_builtin`/`exec_builtin`, `get_magic`, `extension_suffixes`, source-hash, and dynamic-load errors
- [~] `_io`: module exposes VFS-backed `open`, `open_code`, concrete class-name hierarchy shells (`FileIO`, `TextIOWrapper`, buffered classes), `StringIO`, `BytesIO`, file-like context/read/write/seek helpers, iterator hooks, and text newline/encoding basics; exact buffering and full CPython IO internals pending
- [~] `_socket`: constants and socket object lifecycle facade; native networking pending
- [~] `_signal`: signal constants, stateful `signal`/`getsignal`, `raise_signal`, `valid_signals`, `strsignal`, and `default_int_handler` foundations; real OS signal delivery semantics pending
- [~] `select`: `select()` shape for non-network readiness lists; native descriptor polling pending
- [~] `_weakref`: `ref`, `proxy`, `ReferenceType`, `ProxyType`, `getweakrefcount`, `getweakrefs` facade; true weak lifetime/callback semantics pending
- [~] `_collections`: native `deque` foundation with common mutating methods,
  length, iteration snapshots, indexing, and containment; full CPython semantics pending
- [~] `_queue`: native `SimpleQueue` foundation with put/get/qsize/empty and catchable empty errors; blocking semantics pending

High-level modules currently backed by native/runtime code:

- [~] `threading`
- [~] `os`: VFS-backed `listdir`, scandir iterator/context-manager foundation, exported/reused `DirEntry`, `mkdir`, `makedirs`, `remove`/`unlink`, `rmdir`, `rename`, `replace`, `stat`, `access`, `getcwd`, `getcwdb`, `chdir`, `fsencode`/`fsdecode`, plus `getenv`/`fspath` basics; full stat/symlink/dir_fd/environment/error semantics pending
- [~] `os.path` / `ntpath` / `posixpath`: path string helpers foundation with VFS-backed `exists`/`lexists`/`isdir`/`isfile`/`getsize`/absolute resolution plus `split`, `splitext`, `splitdrive`, `join`, `relpath`, `samefile`, `commonprefix`, `commonpath`, `expanduser`, `expandvars`, and CPython-style `abspath("")`/`realpath("")`; exact platform-specific normalization and symlink semantics pending
- [~] `stat`: stat tuple indexes, common constants, permissions bits, and file-type helper functions
- [~] `argparse`: `ArgumentParser` supports constructor keyword basics, public `Namespace`, `add_argument`, option aliases, positional args, defaults, `type=int/float/str`, choices, required options, `store_true`/`store_false`/`store_const`, append/count actions, `nargs` basics, `parse_args`, `parse_known_args`, namespace injection, and usage/help string foundations; full CPython parser/error/help/subparser behavior pending
- [~] `ast`: public `_ast`/`ast` class surface, constructible keyword/positional AST nodes with `_fields`, `dump`, `iter_fields`, `walk`, `NodeVisitor`, `literal_eval` for literal nodes, and parse-result shell foundations; real parser-to-AST lowering and exact CPython node metadata pending
- [~] `code`: `compile_command` uses the XLang3 compiler for complete source and returns `None` for common incomplete REPL blocks; full interactive compiler/console semantics pending
- [~] `codecs`: alias-normalized `lookup`, `getencoder`/`getdecoder`, CodecInfo encode/decode callables,
  UTF-8/UTF-8-SIG/ASCII/Latin-1 encode/decode with strict/ignore/replace/backslashreplace basics, ASCII-compatible
  `idna` lookup/encode/decode foundation, hex encode/decode, and error-handler lookup/registration foundation;
  full codec registry/error handling pending
- [~] `contextlib`: generator `contextmanager`, wrapper metadata (`__name__`, `__qualname__`, `__module__`, `__doc__`, `__wrapped__`) and writable wrapper docs, `nullcontext`, `closing`, `suppress`, `AbstractContextManager`, and native `ExitStack` basics work with with-statements; async helpers and full generator exception propagation semantics pending
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
  `glob.escape`, and VFS-backed `glob`/`iglob` with recursive `**`, iterator-returning `iglob`,
  `root_dir`, `include_hidden`, and bytes-path result preservation; real `dir_fd`,
  platform-specific path normalization, and exact CPython path edge cases pending
- [~] `functools`: `update_wrapper`, `wraps`, `partial` callable/inspection metadata, `reduce`, `cmp_to_key`, `total_ordering`, and real
  positional-call `lru_cache`/`cache` wrappers with bounded eviction, `cache_info`, `cache_clear`,
  and `cache_parameters` foundations; keyword-call caching, exact `CacheInfo` namedtuple behavior,
  `singledispatch`, descriptor edge cases, and full CPython semantics pending
- [~] `__future__`: feature names, `_Feature` metadata/method basics, `__all__`, and public
  `CO_FUTURE_*` compiler flag constants; compiler integration is parser/runtime-owned
- [~] `getpass`: `getuser` uses host environment lookup, `GetPassWarning` is exposed,
  and password readers accept positional/keyword CPython-shaped arguments; real terminal echo control pending
- [~] `itertools`: finite foundations for `count`, `islice`, `takewhile`, `dropwhile`,
  `filterfalse`, `compress`, `repeat(times)`, lazy `chain` with `__next__`, `batched`,
  `product`, `combinations`, `combinations_with_replacement`, `permutations`, `accumulate`,
  `starmap`, `zip_longest`, `pairwise`, and snapshot-backed `tee`; full lazy object identity
  for every helper, keyword-only options, and complete iterator algebra pending
- [~] `json`: native `loads`/`load`/`dumps`/`dump`, file-like I/O, CPython-style default separators,
  `indent`, `sort_keys`, `ensure_ascii`, `separators`, `skipkeys`, `default`, `object_hook`,
  `object_pairs_hook`, `parse_int`, `parse_float`, `JSONEncoder.encode`/`iterencode`, and
  `JSONDecoder.decode` foundations; exact `JSONDecodeError` payloads, `allow_nan`/`parse_constant`,
  streaming encoder details, and full CPython package behavior pending
- [~] `locale`: category constants, set/get locale, encoding helpers, normalize,
  localeconv shape, `strcoll`, `strxfrm`, `localize`, `delocalize`, `atoi`,
  `atof`, and `CHAR_MAX`; real platform locale semantics pending
- [~] `marshal`: XLang3-native `dumps`/`loads` and file `dump`/`load` round-trip foundations for scalars,
  strings/bytes, and common containers; this is intentionally not CPython `.pyc`/code-object marshal exact yet
- [~] `numbers`: numeric ABC hierarchy is backed by `ABCMeta`, builtin numeric scalar types are registered
  across the CPython-style `Number`/`Complex`/`Real`/`Rational`/`Integral` lattice, and user virtual
  subclass registration works with `isinstance`/`issubclass`; exact abstract method surface and complex
  numeric protocol edge cases pending
- [~] `opcode`: public opcode map/name foundation, CPython 3.14 category-list constants, `cmp_op`, `EXTENDED_ARG`, and `_opcode` helper facade; full CPython opcode table/disassembly metadata pending
- [~] `operator`: arithmetic, in-place and `__dunder__` aliases, bitwise, comparison,
  `call`, `abs`, truth/identity/contains, item mutation, length/count/index helpers,
  magic-method item fallback, and attr/item/method getter foundations; full CPython edge cases pending
- [~] `pickle`: public `pickle` and `_pickle` expose protocol constants, exceptions/classes,
  `Pickler`/`Unpickler`, and `dumps`/`loads`/file `dump`/`load`; new output uses a CPython-readable
  pickle opcode subset for common scalars/bytes/strings/containers, and XLang3 reads the same protocol-4
  subset from CPython; reducers, persistent IDs, shared-reference memo semantics, custom object state,
  extension registry, and protocol-5 out-of-band buffers pending
- [~] `platform`: platform/python version helpers, build/compiler/branch/revision metadata,
  node lookup, `uname()` object, `architecture()`, `libc_ver()`, `win32_ver()`, `mac_ver()`,
  `java_ver()`, `system_alias()`, `_sys_version()`, and `freedesktop_os_release()` foundations;
  exact OS-release probing and CPython namedtuple identity pending
- [~] `pkgutil`: VFS/import-root `iter_modules`, `walk_packages`, `extend_path`, `get_data`,
  `resolve_name`, and loader placeholder foundations; named `ModuleInfo`, full finder/loader semantics,
  zip/resource edge cases, and exact import-package behavior pending
- [~] `re`: regex compile/match/search/fullmatch, compiled `Pattern` methods, `Match.group/groups/span/start/end`, bytes-pattern match groups, `findall`, `split`, `sub`, flag aliases, bytes-pattern basics, and `escape` facade; full CPython regex semantics pending
- [~] `signal`: public signal facade with constants, stateful handler registration, synchronous `raise_signal`, `valid_signals`, `strsignal`, and catchable `KeyboardInterrupt` from `default_int_handler`; real OS delivery/thread semantics pending
- [~] `site`: site-package path helpers, public path constants, `addsitedir`, and `addsitepackages` foundations; `.pth` processing/startup-site behavior pending
- [~] `socket`: facade over `_socket` constants and socket object basics; connect/bind/send/recv pending
- [~] `queue`: native `Queue`, `LifoQueue`, `PriorityQueue`, `SimpleQueue`, `Empty`,
  `Full`, and `ShutDown` foundations with ordering/maxsize/task helpers,
  keyword-shaped put/get, and `Queue.shutdown()` basics; blocking/wakeup semantics pending
- [~] `string`: public constants, `_string.formatter_parser`/`formatter_field_name_split`, and native
  `Formatter` methods for `format`, `vformat`, `parse`, `get_value`, `format_field`, and `convert_field`;
  full nested-field parsing, keyword formatting, subclass override hooks, and exact CPython formatter behavior pending
- [~] `struct`: native `calcsize`, `pack`, `pack_into`, `unpack`, `unpack_from`, `iter_unpack`,
  `Struct`, and catchable `struct.error` foundations for common endian prefixes plus integer,
  bool, float/double, char, bytes, pascal-string, and pad format units; native alignment,
  exact range diagnostics, keyword forms, true iterator object identity, and full CPython format edge cases pending
- [~] `subprocess`: constants, `Popen` wait/poll/terminate/kill/communicate/context-manager basics,
  `pid`/`args`/`returncode` metadata, `run()` with Windows child launch, concurrent pipe draining,
  `capture_output`/`stdout=PIPE`/`stderr=PIPE`/`stderr=STDOUT`/`DEVNULL`, `input`, `timeout`,
  `shell`, `CompletedProcess`, and catchable `CalledProcessError`/`TimeoutExpired` foundations;
  POSIX process launch, exact file-handle inheritance, text-mode `Popen.communicate`, advanced
  lifecycle/session/group semantics, and full CPython edge behavior pending
- [~] `sysconfig`: path names/dicts, platform/version, scheme name/default/preferred helpers,
  `_get_preferred_schemes`, `_expand_vars`, `_get_sysconfigdata_name`, `is_python_build`,
  config filename helpers, common config-var helpers, and makefile-variable expansion;
  full install scheme compatibility pending
- [~] `typing`: common aliases, identity decorators, `TypeVar`, `NewType`, `Generic`, and `Protocol` foundations; parsed type-parameter bounds/defaults/variance/lazy evaluation and full typing runtime behavior pending
- [~] `traceback`: `format_exception`, `format_exception_only`, `format_exc`, `print_exception` basics; exact frame/line formatting pending
- [~] `tokenize` / `_tokenize`: CPython `tokenize.tokenize()` can consume byte readline callables through `_tokenize.TokenizerIter`, namedtuple `TokenInfo._make`, callable-sentinel `iter`, lazy `itertools.chain`, native COMMENT/NL preservation for comment-only, blank, and inline-comment lines, and UTF-8/UTF-8-BOM/ASCII/Latin-1 coding-cookie handling; exact token text for string literals and full CPython tokenizer parity pending
- [~] `linecache`: VFS-backed `getline`, `getlines`, `updatecache`, `clearcache`, `checkcache`, and `lazycache` foundation with UTF-8/UTF-8-BOM/ASCII/Latin-1 coding-cookie decoding; exact cache invalidation semantics pending
- [~] `inspect`: common predicates, `currentframe`/`stack` placeholders, `getfile`/`getabsfile`,
  `getmodule`/`getmodulename`, `getmro`, doc cleanup, unwrap, generator/coroutine state helpers,
  Python-callable `getmembers` predicates, `getfullargspec`, and `signature`/`Signature`/`Parameter`/`BoundArguments`
  foundations for Python functions; exact frame stack, source block slicing, keyword binding, annotations,
  descriptor classification, and full CPython signature semantics pending
- [~] `runpy`: `run_module` and `run_path` basics returning globals dict snapshots
- [~] `importlib`: `import_module`, `invalidate_caches`, `importlib.util.find_spec`/`resolve_name`,
  loader/spec/module creation foundations, and VFS-backed `importlib.resources` read helpers
- [~] `types`: `ModuleType`, `SimpleNamespace`, `MethodType` basics; exact CPython type objects pending
- [~] `collections`: native `deque` with iteration/index/containment, `defaultdict`,
  `OrderedDict`, `namedtuple` with `_make`, dict-backed `Counter`, and `ChainMap` foundations;
  full CPython collection semantics pending
- [~] `weakref`: facade over `_weakref` basics plus `finalize` placeholder; true weak lifetime/callback semantics pending
- [~] `logging`: native logger facade with levels, `basicConfig`, root functions, `getLogger`, level-name helpers, Logger methods/effective-level checks, and no-op Handler/StreamHandler/NullHandler/Formatter classes; real handler/formatter hierarchy pending
- [~] `pathlib`: `Path`/`PurePath` facade with VFS-backed exists/read/write checks, CPython-style `name`/`stem`/`suffix`/`suffixes`/`parts`/`parent` properties, text/binary read/write, `with_name`, `with_suffix`, `/` join via native `__truediv__`, `absolute`, `resolve`, `mkdir`, `unlink`, `iterdir`, `glob`, `rglob`, `match`, `__fspath__`, and string/repr basics; full platform-specific pathlib edge semantics, permissions, symlink behavior, and lazy iterator details pending
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
  attributes, iteration over canonical members, member string display, direct
  `__repr__`/`__str__`, `Flag`/`IntFlag` bitwise operators, inversion over the
  defined flag mask, composite-member naming, and `unique` duplicate rejection.
  Remaining work: CPython-exact metaclass behavior, builtin `repr()` dispatch
  through special methods, richer decorators, pickling-facing helpers, and the
  full boundary/error-policy matrix.

- [~] inherited builtin constructor audit:
  subclasses of `int`, `str`, `float`, and `bytes` route through builtin
  constructors and preserve class-level constants, but currently return base
  scalar/bytes values rather than subclass instances; CPython-compatible boxed
  scalar subclass identity/arithmetic/string behavior remains pending.

- [~] `os.scandir` / `DirEntry` audit:
  `os.scandir()` now returns a native scandir iterator with `__iter__`,
  `__next__`, `close`, and context-manager methods; `os.DirEntry` exposes
  `name`, `path`, `inode`, `is_file(follow_symlinks=...)`,
  `is_dir(follow_symlinks=...)`, `is_symlink`, and `stat`. Fixtures cover
  iterator behavior, context manager cleanup, explicit `next()`, keyword
  follow_symlinks forms, path-like arguments, and bytes paths. Remaining work:
  true VFS symlink/follow semantics, platform-exact inode metadata, exact stat
  payloads, and CPython-exact OSError subclasses/diagnostics.

- [~] function metadata audit:
  Covered in fixtures: docstrings, positional `__defaults__`, keyword-only
  `__kwdefaults__`, direct assignment for those defaults, annotations
  assignment, custom function attrs, explicit assignment through live
  `__dict__`, `__globals__`, `__closure__`, `__code__`, and `__qualname__`
  basics for module functions, nested functions, class methods, nested
  class methods, bound methods, raw `staticmethod`/`classmethod` wrappers,
  class `__dict__` inspection, and native/builtin function metadata such as
  `__name__`, `__qualname__`, `__module__`, defaults, annotations, and custom
  wrapper attrs. Still pending: CPython-exact code object completeness, live
  mappingproxy semantics for class dictionaries, exact read-only/error
  behavior on method descriptors, and builtin native function descriptor edge
  cases.

- [~] tokenizer/string-literal audit:
  Section fixture covers raw strings, bytes escapes, f-strings, adjacent
  literals, triple strings after expressions, comments, escaped quotes, and
  triple quote sequences inside normal strings, plus triple-quoted literals
  with suffix/chained calls inside implicit line continuation. `tokenize`
  fixture coverage now also checks COMMENT/NL tokens while preserving `#`
  inside string literals. Remaining work: exact source-token text for strings,
  broader tokenizer parity against CPython `Lib/tokenize.py`.

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
