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
- Scoped regex flags such as `(?i:...)` are noncapturing groups. A fallback
  normalizer must preserve that group numbering and restore the outer flag
  state after the closing parenthesis; flattening the scope into `(...)`
  changes both matching and every later group reference.
- Nested VM execution must link saved outer frame states into the materialized
  `f_back` chain. Warning `stacklevel` and other frame-walking APIs otherwise
  stop at the imported module or report a synthetic system filename.
- `str.format_map` must call the supplied mapping's `__getitem__`; direct dict
  storage lookup loses subclass `__missing__` behavior and non-dict mappings
  such as regex Match objects.
- A regex full match must ask the backend to match the complete candidate.
  Taking the first prefix returned by search and rejecting it prevents later
  alternation branches from consuming the full string.
- Zero-width regex loops must test the search cursor against the input length
  before constructing iterator ranges. A failed synthetic lookbehind at EOF can
  advance one byte past the buffer even when successful empty matches are
  otherwise handled correctly.
- Native regex substitution should run the real `re._parser.parse_template`
  validation before expansion. A placeholder `_sre.template` result bypasses
  invalid-escape and group-reference checks performed by Python 3.14.
- A reserved but unset module slot must remain absent from `dir(module)` and
  `vars(module)`. Advertising an invalid `__path__` makes generic discovery
  code treat ordinary modules as packages and then fail on `getattr`.
- Optimized `obj.method(...)` lowering must use `__getattr__` after ordinary
  lookup fails, just as a separate `method = obj.method; method(...)` sequence
  does. Stdlib stream decorators depend on delegated calls such as
  `wrapper.write(...)`.
- A `with` statement must retain the original exception value across the
  `__exit__` call. The exit method may catch and clear its own active exception
  while returning false, so lowering a bare re-raise afterward loses the
  exception; raise the saved value instead.
- `generator.throw()` has two exceptional completion paths: an uncaught
  injected exception must propagate unchanged, while a generator that handles
  it and returns must raise `StopIteration` with the return value.
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

- Keep dictionary-subclass mapping entries separate from the instance attribute
  dictionary. Apply the distinction in normal and optimized VM attribute access;
  otherwise `defaultdict` attributes leak into values and break source-backed
  `importlib.metadata` discovery. Preserve the separate dictionary through the
  instance graph codec and add assertions without changing expected output.
- Path-hook registrations are cumulative. A later native module must append its
  hook instead of replacing `sys.path_hooks`, or fixing filesystem discovery
  will silently disable zip imports (and vice versa).
- Custom native `__new__` methods need the same positional and keyword argument
  set as the subsequent Python `__init__`. Preserve copied keyword values when
  constructors such as `threading.local` replay initialization in other threads.
- A module-level `__getattr__` miss may leave a pending `AttributeError` even
  when `getattr(module, name, default)` returns its default. Consume that exact
  exception before returning or later unrelated code will fail with a stale
  error.
- When a logical line spans a bracketed expression and contains a multiline
  triple-quoted string, update bracket depth from the text before the opener and
  again from the suffix after the closer. Processing only the suffix can close
  the outer bracket early and split the remaining expression into invalid lines.
- Regex verbose mode is scoped state. Remove whitespace and comments while the
  mode is active, restore the previous state at `)`, and retain literal spaces
  inside `(?-x:...)`; a one-time whole-pattern whitespace pass cannot implement
  nested verbose scopes.
- Native failures that Python code is expected to catch must create the matching
  exception object. Returning `false` with `"no such group"` is not equivalent
  to raising `IndexError`, especially in loops that probe group numbers.
- Python pattern escapes use one and two decimal digits for capture
  backreferences, while three octal digits form a character escape. Treating
  every `\\1`-style token as octal silently breaks most backreference matching.
- CPython `re.Scanner` compiles a constructed `SubPattern`, so `_sre.compile`
  receives `pattern=None` plus the emitted opcode list. Support that native
  representation at `_sre`; changing `Lib/re` or inventing a public scanner
  facade would bypass the source-backed compatibility contract.
- A native pickle writer must consult an object's reduction protocol once the
  builtin scalar/container cases are exhausted. Serializing the returned global
  callable and argument tuple with standard `GLOBAL` and `REDUCE` opcodes keeps
  source-backed reducers such as `re.Pattern.__reduce__` authoritative.
- A byte-oriented fallback regex engine must wrap each multibyte UTF-8 code
  point as one atom before applying character classes or quantifiers. Convert
  engine byte offsets back to code-point indices for Python Match spans, while
  converting those indices to byte offsets again when slicing stored text.
- VM constructor fast paths must preserve decoding semantics and encoding
  aliases. Raw-copying `str(bytes, 'latin1')` creates invalid UTF-8 storage and
  can make a source-backed parser consume a following delimiter as part of the
  malformed character.
- Pattern compilation and matching have distinct buffer contracts, but both
  must recognize true immutable string/bytes subclasses through their owned
  builtin payload. Keep bytearray and memoryview acceptance limited to match
  subjects rather than broadening the compile contract.
- Numeric escape classification must track character-class scope in every
  layer, including post-match semantic validation. A token such as `\\10`
  inside `[...]` is an octal literal and must never be rejected as an unmatched
  group reference.
- Avoid broad regex rewrites after normalization. Parse the specific construct
  that the byte backend cannot represent, such as an escaped pure-literal
  class, and leave escaped bracket classes intact for stdlib users like
  `zipfile`.
- Backend dot semantics are not interchangeable. Python dot excludes only
  `\\n` unless DOTALL is active, while the MSVC ECMAScript engine also excludes
  carriage return; normalize ordinary dot to an explicit `[^\\n]` class.
- Test infrastructure failures can conceal a much smaller compatibility set.
  CPython's regex escape matrix constructs cases with old-style `%c`; implement
  that core string protocol before treating every resulting subtest error as a
  separate matcher defect.
- Built-in modules such as `gc` are legitimate native runtime dependencies,
  while their Python consumers must remain source-backed. Expose coherent
  runtime state and collections instead of adding a public pure-Python facade.
- Python `chr()` accepts lone surrogate code points even though they are not
  Unicode scalar values. Internal UTF-8 storage must preserve that Python value
  domain rather than rejecting the surrogate range during construction.
- Translate Python's omitted repeat minimum `{,N}` to `{0,N}` only after the
  braces have been classified as a quantifier. The CPython overflow test also
  formats enormous bounds through `%d`, so arbitrary-precision decimal
  formatting is part of reaching the matcher contract.
- A helper for one complete character class must reject unescaped internal
  closing brackets. Otherwise adjacent classes such as `[\\a][\\b]` collapse
  into an alternation and a successful match stops after the first character.
- `bytes(text, encoding)` must encode Unicode code points rather than copy the
  runtime's UTF-8 storage. In particular, ASCII must raise for U+0080 and above,
  while Latin-1 maps U+0000 through U+00FF to single bytes.
- Named regex escapes are parsed by source-backed `re` but the original pattern
  still reaches `_sre`. Resolve `\\N{name}` through the native Unicode-data
  dependency so regex normalization and `unicodedata.lookup()` share one name
  table.
- Named tuples and similar tuple subclasses store their sequence payload behind
  the instance. Lexicographic comparison must use the tuple-backed view, just
  as concatenation already does, so source-backed sorters can order them.
- A byte regex backend can expose code-point spans while still returning a raw
  byte endpoint inside a multibyte character. Substitution cursors must advance
  through the remaining continuation bytes before appending the unmatched tail.
- Arithmetic should try the common integer-like conversion for native ints,
  big integers, and int subclasses. Restricting that path to explicit BigInt
  operands rejects ordinary expressions such as `2 * IntEnumMember`.
- Unicode `\\d` means the `Nd` general category, not every numeric character.
  Generate its UTF-8 alternatives from the versioned decimal-digit ranges and
  keep the backend's ASCII category for bytes and `re.ASCII` patterns.
- Translating `\\A`, `\\Z`, or `\\z` to backend line anchors is insufficient
  under multiline mode. Track the absolute-anchor requirement in Pattern state
  and validate the final byte span against the complete subject.
- Unicode `\\b` and `\\B` depend on the word classification on both sides of
  the zero-width position. Validate edge assertions against decoded code points,
  and pass `match_prev_avail` when resuming zero-width iteration so a suffix
  search does not forget the character immediately before its cursor.
- After returning an empty regex match, Python tries a nonempty alternative at
  the same position before advancing. Keep the search cursor separate from the
  unmatched-output segment in split and substitution, and persist this retry
  state across find-iterator calls.
- Post-match assertions extracted from one alternation branch must not be
  applied to a consuming sibling branch. Record when an extracted boundary or
  lookbehind belongs to an empty-only branch, and validate it only for empty
  matches.
- Mapping proxies must raise the same catchable `KeyError` as dictionaries on a
  missing subscription. A raw mapping error bypasses source-backed exception
  translation such as `re._parser`'s unknown-group handling.
- Unicode identifier validation must decode code points and distinguish letters
  from arbitrary non-ASCII bytes. Supplementary mathematical letters are valid
  identifier starts; symbols and decimal digits are not.
- When a lazy backend match fails an extracted trailing lookbehind, retry longer
  matches from the same start before advancing the search cursor. Otherwise an
  escaped delimiter causes the valid later delimiter to be skipped entirely.
- An anchored pattern with a mismatching first literal can answer before copying
  a large subject. Apply that fast miss consistently to every regex API, while
  still validating replacement templates before an unmatched substitution.
- `Match.lastindex` identifies the group that closed last, not the highest
  numbered matched group. Record group-closing order while parsing capture
  syntax so nested captures report their outer group correctly.
- Public modules implemented as CPython extension modules, such as `array`, are
  valid native dependencies. Back them with owned typed storage and real
  mutation/serialization methods so buffer consumers receive coherent bytes;
  an import-only shell would not satisfy the compatibility contract.
- Regex category and case-fold expansion must be gated by the pattern's Unicode
  mode. Expanding Latin-1 letters for string `\\w` and ignore-case literals is
  correct only when neither bytes semantics nor `re.ASCII` is active.
- Unicode ignore-case is an equivalence relation, not a single upper/lower pair.
  Close special multi-member fold classes for literals, escapes, sets, and
  ranges, and do not delegate cross-category ranges such as `[9-A]` to a host
  regex engine whose case-insensitive range expansion includes punctuation.
- Scoped regex `a`, `u`, and `-i` flags need state restoration alongside `s`,
  `m`, and `x`. For UTF-8-backed strings, an ASCII-mode `\\W` atom must
  consume one complete non-ASCII code point rather than one storage byte.
- Regex Match results over mutable buffers retain the subject and slice its
  live contents using the recorded spans. Iterators additionally own a buffer
  export so resizing is rejected while matching can still access its storage;
  release that export on exhaustion as well as destruction.
- Large fixed-width lookbehinds should carry a calculated width constraint into
  searching instead of expanding millions of identical atoms. This preserves
  zero-width spans and lets very large valid repetition counts compile quickly.
- A conditional assertion must inspect capture state at the assertion point,
  not the final Match state. Record how many captures are defined before the
  assertion so a group defined in its trailing suffix still selects the
  unmatched branch.
- A terminal consuming conditional can preserve public capture numbering by
  normalizing its branches to a noncapturing alternation and validating the
  chosen suffix against capture state. Empty selected branches must also reject
  a nonempty unselected suffix consumed by the host matcher.
- Accepting atomic or possessive syntax is insufficient when the host matcher
  can backtrack through the normalized form. Validate committed alternatives
  and overlapping following atoms, and repair capture history only for captures
  inside repeated ancestors; captures inside a final lookahead branch must reset.
- Optional groups are not repeat-history loops. Empty-final-iteration capture
  repair applies only to quantifiers that can execute more than once and only
  when that repeat is terminal; otherwise it can overwrite valid captures used
  by source modules such as `pkgutil.resolve_name`.
- A capture inside a successful negative lookbehind must retain its public group
  slot but remain unmatched. Normalize it to an impossible optional capture and
  validate the lookbehind against the source text.
- Python named Unicode escapes are lexer behavior as well as regex behavior.
  Decode `\\N{...}` string literals through the same Unicode-name lookup used by
  `_sre`, and account for global inline flags before edge boundary assertions.
- Translating `\\z` to `$` is incorrect under multiline mode when groups follow
  the anchor. Use an absolute negative lookahead for any remaining code point.
  Likewise, a string-pattern dot must consume one complete UTF-8 code point so
  `fullmatch` operates on Python characters rather than storage bytes.
- Native `sys` capability queries must derive from the runtime or build switch
  they describe, and related `_sysconfig` values must use the same source of
  truth. PEP 669 `CALL`, `C_RETURN`, and `C_RAISE` callbacks receive four
  arguments, with `sys.monitoring.MISSING` as the final argument when no call
  argument is available.
- Windows `spawnv` cannot join arguments with spaces: quote each argument with
  the Microsoft command-line backslash rules before `CreateProcessW`. Native
  `nt` failures should preserve both POSIX-facing `errno` and the original
  `winerror`, plus `strerror` and `filename` where applicable.
- Formatting an instance with `!r` must fall back to `object.__repr__` when its
  class hierarchy provides no custom representation. Source-backed virtual
  path implementations rely on that fallback when constructing validation
  errors for empty paths.
- Three-argument modular exponentiation must avoid signed multiplication
  overflow even when every operand fits in 64 bits. Compilation in `single`
  mode must also retain that mode for expression input so evaluation resolves
  and invokes the current `sys.displayhook` dynamically.
- Frame and traceback objects expose live activations: `f_code` identity must be
  stable, traceback `tb_lineno` stays at the raise site, while
  `tb_frame.f_lineno`, locals, and `f_back` follow the running call stack.
- Integer literals must enter the arbitrary-precision path during lowering.
  Deferring bigint creation until arithmetic lets a large decimal literal
  overflow before the runtime can preserve it.
- Platform `strftime` cannot be called once with an arbitrary Python string:
  embedded NULs, wide years, negative years, and Unicode literals require
  directive-by-directive formatting while preserving untouched string data.
- Zero-argument `super()` must identify the function's defining class through
  property getters as well as direct methods. A property cloned onto a subclass
  can retain a getter defined on its parent; falling back to the receiver class
  makes that getter resolve back to itself and recurse.
- Built-in descriptor subclasses need direct payload dispatch for inherited
  `classmethod.__get__` and `staticmethod.__get__`. Redispatching the proxy by
  looking up `__get__` on the same subclass instance recursively calls the proxy.
# Import compatibility lessons

- Treat `type` as a soft keyword when parsing PEP 695 aliases. Evaluating a
  non-generic alias RHS as a normal assignment is sufficient for source-backed
  stdlib imports until full `TypeAliasType` metadata is implemented.
- Loader path arguments must honor `os.PathLike`, including construction,
  source reads, path statistics, compilation, and atomic bytecode writes.
  `py_compile` and compiled-only resource packages exercise the whole chain.
- Namespace package paths must be recalculated after `sys.path` changes, while
  negative directory lookups remain cached until import cache invalidation.
  ZIP namespace detection must infer directories from member prefixes because
  archives are not required to contain explicit directory entries.
- `itertools.filterfalse` must be lazy. Eagerly returning a list breaks stdlib
  consumers such as implicit caller inference in `importlib.resources`.
