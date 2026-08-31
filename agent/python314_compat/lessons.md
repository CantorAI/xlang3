# Compatibility Loop Lessons

Keep this file short. Add only reusable lessons that should shape future
batches.

## Product Direction

- The goal is Python 3.14 runtime compatibility. Do not optimize for debugpy,
  benchmarks, or isolated fixture tricks.
- Pure Python CPython stdlib modules should run from the real `Lib/*.py` source.
  Do not add public C++ facades for modules such as `asyncio`, `ctypes`,
  `threading`, `warnings`, `signal`, `abc`, `collections`, `queue`, `json`,
  `pathlib`, `inspect`, `argparse`, `typing`, `subprocess`, or `zipfile`.
- Native C++ is for XLang3 runtime primitives, builtins/builtin types, CPython
  native dependency modules, and product-specific accelerated modules.
- When a pure stdlib import fails, fix the runtime primitive or native
  dependency it exposes. Do not make the import pass with a stub.
- The `ctypes` cleanup is the canonical example: do not register native
  `ctypes`/`ctypes.wintypes`; implement `_ctypes` and let CPython's
  `Lib/ctypes` package run.

## Validation

- Every compatibility claim needs fixture coverage under `tests/fixtures`.
- Do not update expected output just to pass. Compare with Python 3.14 behavior
  or the intended XLang3 runtime contract first.
- Treat crashes, hangs, Windows popups, negative exits, and timeout regressions
  as runtime bugs. Add the smallest fixture repro before fixing.
- If validation fails, repair the current batch before advancing to a new task.

## Workflow

- Use deterministic scripts: `agent/scripts/build_release.py`,
  `agent/scripts/run_fixtures.py`, and `agent/scripts/run_section_fixture.py`.
- Use `rg -F` for literal PowerShell searches involving checkboxes, brackets,
  backticks, quotes, C++ punctuation, or Python syntax.
- Do not start a second loop while one is running. Use the loop lock and
  stop-request file.
- Decode captured child process output as UTF-8 with replacement on Windows.

## Runtime Compatibility

- Descriptor changes usually need both generic attribute lookup and VM fast-path
  attribute lookup updated together.
- Native functions that should raise catchable Python exceptions must use the
  runtime exception path, not raw error strings.
- Keyword handling and arity diagnostics are CPython-visible behavior; fixture
  both accepted calls and rejection text when changing call binding.
- Builtin constructor fast paths must preserve `__new__`, `__module__`,
  `__qualname__`, and class statement behavior.
- Exception behavior must follow inheritance, not name suffixes like `Error` or
  `Exception`.
- Structseq behavior belongs in shared helpers. Cover construction policy,
  named fields, repr/str, reduce/pickle payloads, and descriptor behavior.
- Import compatibility includes visible metadata and cache side effects:
  `sys.modules`, `__loader__`, `__spec__`, `__file__`, `sys.path[0]`, and
  `sys.path_importer_cache`.
- `sys.exc_info()` and `sys.exception()` are scoped to active exception
  handlers; nested handlers must restore previous state correctly.
- File/path APIs must preserve CPython's distinct `str`/`bytes`/`os.PathLike`
  conversion errors.
- Text streams return `str`; binary/buffer layers return `bytes`.
- Threading changes must define ownership and locking invariants. Lock the
  minimum region and do not serialize the whole VM as a shortcut.
- Zero-argument `super()` inference must start from the active function's first
  parameter name. CPython stdlib metaclass methods can also have locals named
  `cls` for the class name string, so probing `self`/`cls` first can bind the
  wrong object.
- Python callbacks triggered by `type.__new__`, such as descriptor
  `__set_name__`, must run after the native call trampoline has reacquired the
  VM execution lock. Do not invoke Python descriptor callbacks from inside the
  native callback while the execution guard is released.
- Builtin constructor fast paths must be keyed by the registered builtin class
  object, not by `ClassObject::name`. CPython stdlib can define user classes
  named `property`, `str`, or other builtin names, and those classes must
  construct normal user instances.
- Prepared namespace writes must route through the namespace object's
  `__setitem__`. Writing directly into dict storage bypasses metaclass
  bookkeeping such as `EnumDict` member tracking.
- Singleton and builtin special-method sentinels used by stdlib identity tests
  must be stable across attribute lookups. Returning a fresh native function for
  `None.__new__` breaks `target in {None.__new__, object.__new__, ...}` checks.
- Multiple-base classes must preserve every explicit base in `__bases__` and
  C3 MRO calculation. Dropping the first base from `StrEnum(str, ReprEnum)`
  hides builtin `str.__new__` and makes real `enum.py` choose the wrong member
  construction path.
- Integer-only operations must keep integer results in this runtime's numeric
  model. Approximating overflowed left shifts as `Double` breaks stdlib probes
  such as `_collections_abc` building a long `range()` iterator type.
- Callable checks used by descriptor constructors must include classes, matching
  the runtime call path and `callable()`. CPython stdlib wraps class objects
  such as `type(list[int])` with `classmethod(...)`.
- `type()` metadata for iterator objects must resolve to class objects, not
  constructor builtins. `_collections_abc` registers iterator probe types with
  `ABCMeta.register()`, which correctly rejects non-class values.
- Operator support and method exposure must both be checked for container
  protocols. Pure stdlib modules bind dunder methods directly, for example
  `keyword.py` stores `frozenset(...).__contains__`.
- Parser support for Python operators must include both expression syntax and
  lowering/runtime dispatch. Treating `@` only as a decorator token makes
  stdlib modules with `a @ b` or `a @= b` function bodies fail during import.
- `from builtins import name` must see the runtime builtin registry as well as
  copied module attributes. Pure stdlib modules import builtins such as `abs`
  by attribute name from the `builtins` module.
- Builtin container dunder methods used as default arguments must be present on
  the builtin class, not only implemented by syntax. `collections` binds
  methods such as `dict.__delitem__` while defining pure-Python containers.
- Root `object` dunders are part of stdlib-visible MRO lookup. Pure Python
  ABCs can inherit and rebind methods such as `object.__ne__`.
- Builtin type utility methods can be imported through pure-Python stdlib
  class bodies. `collections.UserString` binds `str.maketrans` directly, so
  class method tables need these utilities even when syntax does not use them.
- Native dependency modules must export the same public names that pure stdlib
  imports consume. `functools` imports `RLock` from `_thread`, not from
  `threading`.
- Callable predicates must match the runtime call path, including instances
  whose class defines `__call__`. Stdlib helpers such as `operator.itemgetter`
  are callable instances, not functions.
- Builtin constructor fast paths need CPython keyword support for stdlib class
  bodies. `collections.namedtuple` calls `property(..., doc=...)` while
  generating field descriptors.
- Generic callback runners must support class callables, not just direct VM
  call opcodes. Functional iterators call their function through
  `runtime_call_callable`, so `map(str, values)` exercises class construction
  outside the opcode fast path.
- Stdlib factory helpers validate names with string predicate methods such as
  `str.isidentifier`; string method coverage should include predicates used by
  generated-class paths like `collections.namedtuple`.
- Enum and flag helpers use integer introspection methods such as
  `int.bit_length`; arithmetic compatibility must include the methods that
  pure stdlib calls on intermediate integer values.
- `object.__init__` argument validation depends on whether the class resolved a
  custom `__new__`. Enum member finalization calls inherited `object.__init__`
  with member values and must not reject that CPython-compatible path.
- Special iteration of class objects uses the metaclass protocol. `iter(Enum)`
  must bind `EnumType.__iter__` even when the enum class also inherits an
  instance-level `__iter__` such as `Flag.__iter__`.
- Module `__dict__` must be the live mutable module namespace. Stdlib helpers
  such as `enum.global_enum` update `sys.modules[name].__dict__` to export
  generated members.
- Range objects are sequences, not only iterables. Regex compilation indexes
  and slices ranges while building internal bitmaps.
- Tuple equality must not require ordering of the first unequal item. Regex
  parser tuples can contain sentinel objects that support identity/equality but
  not `<`.
- Legacy sequence-protocol objects with concrete `data` storage appear in the
  stdlib regex parser; low-level list/set expansion paths need a concrete
  sequence fallback when no runtime-aware `__iter__` dispatch is available.
- Bytes APIs used by regex/json need integer byte operands and table
  translation. `bytes.find(int)` and `bytes.translate(256-byte-table)` are
  import-time dependencies, not optional conveniences.
- `_sre.Pattern` methods must cover the operations stdlib modules bind from
  compiled patterns. `json.encoder` requires callable replacement through
  `Pattern.sub`.
- VM protocol fallback helpers must propagate pending typed exceptions back to
  the active frame instead of converting them into `RuntimeError`. Stdlib
  mapping helpers such as `_collections_abc.Mapping.get` rely on catching
  `KeyError` raised by a callee `__getitem__`.
- Native stdlib facade modules must export builtin exception aliases that their
  Python wrappers import directly. Python 3.14 `io.py` expects
  `_io.BlockingIOError`, not only `builtins.BlockingIOError`.
- When a fixture exercises a Python wrapper over a native module, verify visible
  constants and class reprs against the target Python version. Python 3.14's
  `io` wrapper exposes `io.TextIOBase` and `_io.DEFAULT_BUFFER_SIZE == 131072`.
- Fallback `types.py` defines `SimpleNamespace = type(sys.implementation)`.
  The class used for `sys.implementation` must therefore implement
  keyword-based namespace construction rather than relying on permissive
  `object.__init__` behavior.
- Fallback `types.py` also defines `ModuleType = type(sys)`. The builtin
  `module` class must construct real module objects for `ModuleType(name, doc)`;
  generic instance construction followed by `object.__init__` is not compatible.
- Fallback `types.py` defines `MethodType = type(instance.method)`. The builtin
  bound-method class must support `MethodType(function, instance)` by creating
  a real bound method object.
- F-string lexing must track brace depth and nested expression strings. Python
  3.14 stdlib uses expressions such as
  `f'... {_safe_string(e, '__notes__', repr)}'`, where the inner quote must not
  terminate the outer f-string token.
- When lexing an embedded triple-quoted string inside a parenthesized
  expression, suffix line joining must inherit the prefix bracket depth. Stdlib
  calls like `re.compile(r'''...''' % {...}, re.VERBOSE)` otherwise emit a
  newline after the first suffix comma.
- Regex/text stdlib imports use both bytes and string translation APIs. Python
  3.14 `re.escape` calls `str.translate` with integer codepoint keys mapped to
  replacement strings.
- Python 3.14 regex patterns use `\z` as an end-of-string anchor. Native regex
  shims backed by engines without `\z` support must normalize that escape
  without rewriting escaped literal backslashes.
- `_sre.compile` receives patterns that CPython's Python-level compiler has
  already accepted. Do not reject valid stdlib patterns solely because a
  fallback backend like `std::regex` lacks features such as lookbehind; track
  fallback executability separately from pattern construction.
- Stdlib wrapper modules may import private C accelerators only for type
  surfaces during unrelated workflows. Python 3.14 `tokenize.py` requires an
  `_tokenize.TokenizerIter` export even when traceback formatting does not
  actually request token generation.
- Traceback imports private helper modules such as `_colorize` for optional
  behavior. If a private native dependency is required, implement that private
  dependency directly; do not register public stdlib modules such as
  `traceback`, `warnings`, or `tokenize` as C++ facades.
- `itertools.islice` must accept `None` for an unbounded stop and return an
  iterator, not a materialized list. Traceback uses
  `next(islice(code.co_positions(), instruction_index // 2, None))`.
- Sequence iterator objects must themselves be iterable. Returning an iterator
  from helpers such as `itertools.islice` requires `iter(iterator) is iterator`
  semantics for subsequent `for` loops.
- `yield` without `from` takes an expression list. `yield a, b` must yield the
  tuple `(a, b)`, because stdlib generators such as traceback frame walkers
  unpack nested yielded tuples.
- Loop target parsing must not wrap parenthesized tuple targets twice.
  `for f, (a, b) in ...` needs the inner target to unpack two values, not one
  tuple-valued target.
- Stdlib classes can subclass builtin containers and immediately use container
  methods on `klass()`. `traceback.StackSummary(list)` requires list-derived
  instances to keep subclass identity while delegating list storage for
  `append`, indexing, length, and iteration.
- If a pure stdlib dependency graph exposes many missing runtime features, keep
  fixing the runtime/native dependency surface. Do not replace the public
  stdlib module with a smaller C++ facade.
- `linecache` reads Python source through `tokenize.open`, so a native tokenize
  facade must preserve source encoding behavior for BOM UTF-8 and first/second
  line `coding:` cookies instead of returning a generic binary wrapper.
- Native I/O failures must raise the matching `OSError` subclass when stdlib
  code catches filesystem errors. `linecache.updatecache` expects missing
  source opens to raise `FileNotFoundError`/`OSError`, not a generic runtime
  error.
- Exception matching must handle tuple handlers recursively. Stdlib commonly
  uses `except (OSError, UnicodeDecodeError, SyntaxError):`, and subclasses
  such as `FileNotFoundError` must match the tuple entry.
- Before changing line-oriented expected output, verify newline ownership
  against the target interpreter. `linecache.getline`/`updatecache` entries
  include trailing newlines, so `print(line)` produces visible blank lines.
- For a probe that appears to hang during import, first rerun the smallest case
  with `XLANG3_DIAG_MISSING_IMPORTS=1` or `XLANG3_DIAG_MISSING_LOOKUPS=1`.
  These runtime markers are for diagnosis only; do not convert the missing
  public Python module into a native facade.
