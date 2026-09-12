# System Stdlib Compatibility

Purpose: make CPython 3.14 `Lib/*.py` system modules run naturally on XLang3.
Do not re-create public pure-Python modules in C++. For each row, run the real
CPython library source, identify the missing runtime/native dependency, add
fixture coverage, then update the row truthfully.

- [x] `abc`, `types`, and `enum`
  Coverage: real CPython 3.14 `abc.py`, `types.py`, and `enum.py` import from
  `C:/Python/Python314/Lib`; the `system_stdlib` section fixture asserts
  source-backed module paths, public class/module ownership, `types` descriptor
  type discovery, and `enum.FlagBoundary` values. Runtime coverage added for
  zero-argument `super()` classmethod/metaclass first-argument binding,
  descriptor `__set_name__`, inherited metaclass `__prepare__`, dict-subclass
  prepared namespace writes, function descriptor `__get__`, multiple-base MRO
  preservation, builtin constructor identity when user classes shadow builtin
  names, `object.__format__`/reduction sentinels, stable `None.__new__`, and
  `str.__new__`/immutable-subclass initialization needed by `StrEnum`.

- [x] `io`, `encodings`, and `codecs`
  Coverage: real CPython 3.14 `io.py`, `codecs.py`, and `encodings` package
  import from `C:/Python/Python314/Lib`; the `system_stdlib` section fixture
  asserts source-backed module/package paths, `_codecs.register` search-hook
  support, UTF-8 lookup/encode/decode flow, and VFS-backed import behavior
  without adding public C++ facades for those pure Python modules.

- [x] `collections`, `_collections_abc`, `queue`, and `weakref`
  Coverage: real CPython 3.14 `collections` package, `_collections_abc.py`,
  `queue.py`, and `weakref.py` import from `C:/Python/Python314/Lib`; the
  `system_stdlib` fixture asserts source-backed module/package paths,
  `_collections.deque` iteration into `list`, ABC-backed `UserDict`
  `MutableMapping` recognition, `queue.SimpleQueue` put/get/qsize/empty, and
  `weakref.ref`, `WeakKeyDictionary`, and `WeakSet` referent iteration behavior.
  Runtime coverage added for structural list comparison so source-backed
  collections tests use normal Python equality rather than object identity, and
  for Python iteration protocol priority over legacy instance-storage fallbacks.

- [x] `json`, `pickle`, `copy`, and `copyreg`
  Coverage: real CPython 3.14 `json`, `pickle.py`, `copy.py`, and
  `copyreg.py` import from `C:/Python/Python314/Lib`; the `system_stdlib`
  fixture asserts source-backed module paths plus `json.loads`, `copy.copy`,
  and `pickle.dumps`/`pickle.loads` round trips. Runtime coverage added for
  relative star imports, dict unpacking in literals, exact-builtin constructor
  shadowing, owned user-class `__new__` dispatch for int subclasses, native
  `_sre` import/Pattern/Match basics used by CPython `re`, and missing
  `_struct` exports required by `struct.py`.

- [x] `textwrap`, `_colorize`, `traceback`, `inspect`, `dataclasses`, `linecache`, and `logging`
  Coverage: real CPython 3.14 source modules import from `C:/Python/Python314/Lib`
  and the `system_stdlib` section fixture covers source-backed module paths,
  `inspect.signature(object)`, dataclass construction, `_colorize.can_colorize`,
  and `logging.getLogger`. Runtime/native primitive fixes added for `_tokenize`,
  f-string embedded-expression lexing, annotation storage, tuple subclass
  construction, type/member descriptor lookup, memoryview item sizing, `nt`
  terminal-color probes, bytearray/bytes concatenation, `_sre` character class
  translation, named group backreferences, fixed literal positive/negative
  lookbehind assertions across search/finditer/findall/sub/split,
  `DOTALL`/`MULTILINE`/`IGNORECASE` flag behavior through CPython `re.py`, scoped
  inline ignore-case enabling/disabling without spurious capture groups,
  `Match.expand` named/numeric backreferences, zero-width boundary splitting,
  callable replacements, CPython
  `inspect.py` `CO_*` flag generation through live
  `globals()`, disabled `sys.monitoring` near-zero-cost frame handling, real
  `mappingproxy` construction/iteration/views for `Signature.parameters` and
  class dictionaries, CPython-shaped `f_lasti`/`tb_lasti`/`co_lines` offsets
  for `traceback.py`, `co_positions()` statement-level line/column metadata,
  catchable `ZeroDivisionError`, keyword-compatible `bytes.decode`, dataclass
  frozen/default_factory/slots/inheritance/InitVar/ClassVar basics, correct
  dynamic `exec(..., globals())` module-slot isolation, and logging exception
  formatter output.
  Broader CPython test discovery now imports `unittest` and `test.support` over
  the real 3.14 sources. Supporting runtime fixes cover parenthesized multiline
  imports whose module components are soft keywords, native
  `SimpleNamespace`/`deque` representations, `float.__getformat__`, dynamic
  type `__flags__` for abstract-class inspection, rich comparison of Python
  objects used as `list.sort`/`sorted` keys, stable `isinstance` fallback types
  for functional iterators, absent module-slot filtering in `dir`/`vars`, and
  callable attributes supplied by `__getattr__`.
  Generator `throw()` now propagates uncaught exception identity, raises
  `StopIteration` when the resumed generator finishes, and preserves the saved
  exception when a `with` manager returns false after inspecting it. This lets
  CPython `contextlib.contextmanager` and `unittest` report failures normally.
  Additional native protocol coverage includes bounded `str.count`, keyword
  `str.splitlines(keepends=...)`, multi-index `Match.group`, `Pattern.subn`,
  keyword-compatible Pattern match/search/fullmatch/findall/finditer/split,
  scanner search/match state, Pattern/Match copy identity, `Match.regs`,
  read-only/equality-compatible `Pattern.groupindex`, and CPython-shaped Pattern
  representations. Pattern equality/hash semantics and basic Match
  representations are covered. Pattern pickling now uses its real `__reduce__`
  result through native standard `GLOBAL`/`REDUCE` pickle opcodes, and the full
  CPython `test_pickling` case passes for every supported protocol. Invalid group indices and invalid group-key
  types now raise catchable `IndexError` with CPython-compatible messages.
  `str.format_map` uses the mapping protocol, including Match named-group access
  and mapping `__missing__`. Regex match inputs accept string/bytes subclasses,
  bytearrays, and memoryviews; full-match alternation evaluates the complete
  candidate. Native normalization also handles regex comment groups, unmatched
  literal closing brackets, and escaped ASCII control characters. Zero-width
  split cursor bounds are safe and the complete CPython `test_re_split` case
  passes. `Match.groups(default)` fills unmatched captures, and replacement
  templates are validated by the real Python 3.14 `re._parser.parse_template`
  path and expand named groups plus valid control and octal escapes. Anchored
  zero-width substitution preserves beginning-of-string semantics, and the
  complete CPython `test_basic_re_sub` and `test_sub_template_numeric_escape`
  cases pass. Invalid
  match subjects raise catchable `TypeError` with the actual operand type.
  The current full CPython `test_re.py` run completes all 165 top-level tests
  with zero failures or errors and 14 intentional platform/CPython-only skips
  (down from 136 failures and 120 errors in the original baseline).
  `test_bigcharset` passes and the former missing-`gc` error reaches its strict
  performance assertion. Targeted split, match/getitem,
  pattern comparison, and
  invalid-subject cases pass. String and bytes subclasses expose inherited
  `__getitem__` with catchable index errors for source-backed parsers. Match
  group indices honor `__index__`, including overflow as `IndexError`, and
  mixed string/bytes match and substitution operands raise catchable
  `TypeError`; the corresponding complete CPython tests pass. Numeric pattern
  escapes distinguish one/two-digit backreferences from three-digit octal
  literals, and unmatched mandatory backreferences fail instead of inheriting
  the backend's empty-reference behavior. CPython Match repr, group-reference,
  and substitution-backreference cases pass. End anchors also match before one
  terminal newline and restore scoped multiline state;
  `test_dollar_matches_twice` passes. Int-like `count` and `maxsplit` values,
  including `RegexFlag`, are honored and `test_misuse_flags` passes.
  Constructed CPython SRE programs used by `re.Scanner` now translate their
  branch, capture, character-set, category, literal, and repeat opcodes at the
  native `_sre` boundary; scanner pattern identity and the complete CPython
  `test_scanner` case pass.
  Regex normalization preserves UTF-8 code points as indivisible native matcher
  atoms, including singleton character classes and quantified non-ASCII
  literals, while Match spans expose Python code-point indices. The complete
  CPython string/bytes `re.escape` cases and `test_bug_16688` pass. The VM's
  optimized `str(bytes, encoding)` constructor now performs real Latin-1 and
  ASCII decoding and honors their standard aliases instead of copying arbitrary
  bytes into UTF-8 string storage. `_sre.compile` also reads the owned payload
  of true `str` and `bytes` subclasses; CPython `test_bug_764548` passes.
  Native escape normalization now decodes octal, `\\x`, `\\u`, and `\\U`
  literals into the correct byte or UTF-8 atom, translates simple escaped
  literal classes without corrupting bracket syntax, and excludes numeric
  escapes inside character classes from backreference validation. The complete
  CPython Unicode/bytes literal and character-class literal cases pass.
  Singleton closing-bracket classes (`[]]` and `[\\]]`) normalize to literal
  bracket atoms without disturbing compound escaped classes used by `zipfile`.
  Non-DOTALL dot now excludes only line feed, matching Python semantics rather
  than MSVC ECMAScript's carriage-return behavior; CPython `test_anyall`,
  `test_search_dot_unicode`, and the external carriage-return cases pass.
  Runtime string `%` formatting now implements Unicode-aware `%c` conversion
  for one-character strings and integer code points. This lets CPython's real
  invalid-escape test matrix execute instead of failing during test-data
  construction; the complete `test_other_escapes` case passes.
  Python's omitted-minimum repeat form `{,N}` normalizes to the backend's
  equivalent `{0,N}` form. Old-style decimal formatting accepts arbitrary-
  precision integers, so the complete CPython `test_repeat_minmax_overflow`
  case reaches and passes both its large matches and overflow checks.
  Decimal backreferences consume at most two digits when they are not a
  three-digit octal escape, so `\\119` correctly means group 11 followed by
  literal `9`. Adjacent singleton control-character classes are normalized one
  class at a time instead of being mistaken for one large alternation.
  `bytes(str, encoding)` now enforces ASCII and Latin-1 code-point ranges,
  raises `UnicodeEncodeError` under strict handling, and supports `ignore` and
  `replace`; this removes the corpus's spurious non-ASCII bytes-regex path.
  The native Unicode-data dependency supplies the character names required by
  named regex escapes, and `_sre` resolves `\\N{name}` atoms and ranges through
  that shared lookup. The complete CPython `test_named_unicode_escapes` case
  passes, including supplementary characters and malformed-name errors.
  Tuple subclasses participate in lexicographic ordering, allowing the real
  `difflib` sorter to process named tuples; `test_possible_set_operations`
  passes and other affected cases reach their matcher assertions. Integer
  subclasses also participate in arithmetic multiplication, and the complete
  CPython `test_bug_725149` case passes. Unicode substitution cursors advance
  past the continuation bytes of a matched code point; `test_bug_6509` passes while
  the existing dot behavior tests remain green.
  Unicode `\\d` is generated from all 760 Unicode 16.0 `Nd` code points while
  bytes and `re.ASCII` retain ASCII semantics; the complete CPython
  `test_bug_6561` case passes.
  Absolute `\\A`, `\\Z`, and `\\z` anchors carry explicit start/end
  validation through the native matcher, preserving their meaning under
  multiline mode; the complete CPython `test_special_escapes` case passes.
  Unicode boundary assertions at pattern edges use Unicode word-character
  classification, while bytes and `re.ASCII` retain byte semantics. Zero-width
  iteration preserves the previous-character context at nonzero cursors; the
  complete CPython `test_word_boundaries` case passes.
  Empty matches now permit a consuming alternative at the same position before
  the search cursor advances, matching Python's split, substitution, findall,
  and finditer rules. Word-category lookbehind assertions distinguish `\\w`
  from a literal `w`, and zero-width branch assertions no longer reject a
  consuming sibling alternative. The complete CPython `test_zerowidth` case
  passes, with deterministic section-fixture coverage for all four APIs.
  Mapping-proxy subscription misses raise `KeyError`, allowing the source-backed
  replacement parser to translate unknown group names correctly. Unicode-aware
  identifier checks reject symbols and numeric starts while accepting Greek and
  mathematical-letter names; the complete CPython symbolic group and template
  error cases pass. Fixed-width lookbehind recognizes capture backreferences,
  locates trailing assertions from the actual match end, and retries longer
  lazy matches from the same start after a rejected assertion. The escaped-
  delimiter external-corpus cases now return their complete captures.
  Anchored literal misses inspect direct string/bytes storage before copying a
  subject, across search, split, findall, finditer, and substitution; the strict
  10-million-character `test_search_anchor_at_beginning` case passes at about
  0.043 seconds against its 0.1-second limit. Match compilation records capture
  close order, so nested outer groups correctly determine `lastindex` and
  `lastgroup`; the complete CPython `test_bug_527371` case passes.
  The native `array` dependency now owns typecode-aware byte storage and exposes
  append, frombytes, tobytes, length, indexing, itemsize, and buffer matching.
  The complete CPython `test_empty_array` case passes across every requested
  typecode, and the section fixture exercises nonempty byte-array mutation and
  regex matching as well as empty arrays.
  Unicode string-pattern normalization expands Latin-1 word characters and
  case pairs while bytes patterns and `re.ASCII` remain ASCII-only. The complete
  CPython `test_ascii_and_unicode_flag` case passes, with fixture assertions for
  both Unicode and ASCII-mode outcomes.
  Unicode case folding now closes the special `K/k/K`, `S/s/ſ`,
  Cyrillic `В/в/ᲀ`, and `ﬅ/ﬆ` equivalence classes for literals, escaped
  literals, sets, and ranges. Runtime `str.lower()` and `str.upper()` provide
  the corresponding Unicode mappings, including the two-character `ST`
  expansion. Range normalization avoids the host engine's punctuation widening,
  and scoped `a`, `u`, and disabled-`i` modes restore their enclosing state.
  The complete CPython `test_ignore_case`, `test_ignore_case_set`,
  `test_ignore_case_range`, `test_inline_flags`, and `test_scoped_flags` cases
  pass.
  Regex Match objects retain their original mutable subject and read group
  slices from its current contents, while find iterators hold a bytearray buffer
  export until exhaustion or destruction. The complete CPython
  `test_bug_29444` and `test_keep_buffer` cases pass.
  Fixed-width dot-repeat lookbehinds compute nested repetition widths without
  expanding their bodies. Positive searches begin at that width, negative
  searches remain valid at zero, and oversized repetition operands retain the
  source parser's overflow checks; the complete CPython
  `test_look_behind_overflow` case passes.
  Conditional lookahead and lookbehind assertions select fixed-width branches
  from the capture state available at the assertion point, including references
  to groups defined later. The complete CPython `test_lookahead` and
  `test_lookbehind` cases pass.
  Terminal consuming conditionals choose literal or empty branches from the
  captures available at their position, supporting numeric and named group
  conditions beyond group 100. The complete CPython `test_re_groupref_exists`
  and `test_symbolic_groups` cases pass.
  Atomic groups and possessive quantifiers preserve commit points across match,
  fullmatch, search, and findall while repeated alternatives retain or reset
  nested captures according to Python's mark-stack behavior. The complete
  CPython atomic/possessive suites, `test_bug_gh91616`, `test_bug_gh100061`,
  `test_bug_gh101955`, `test_bug_725106`, `test_MIN_REPEAT_ONE_mark_bug`,
  `test_MIN_UNTIL_mark_bug`, and `test_bug_2537` pass.
  Captures inside a successful negative lookbehind preserve public group
  numbering while remaining unmatched. Python string literals decode named
  Unicode `\\N{...}` escapes through shared native Unicode-name data, and
  Unicode boundary assertions recognize a leading global inline-flag group.
  Absolute `\\z`/`\\Z` normalization uses a true end assertion even under
  multiline mode with trailing captures. Dot atoms consume one complete UTF-8
  code point, including through `fullmatch`. These changes complete the legacy
  external regex corpus as well as the regular CPython regex test classes.
  The native `gc` dependency now exposes coherent enabled state, collection
  counters, thresholds, `garbage`, and `callbacks`, allowing source-backed
  stdlib consumers to import and exercise the built-in API. `chr()` also
  accepts the full Python code-point range, including lone surrogates; the
  complete CPython `test_bigcharset` case passes. The section fixture covers
  both behaviors and the full deterministic fixture suite remains green.
  Native warnings delegate filtering and recording to the real `_py_warnings.py`
  implementation. Runtime frame snapshots now link nested imported-module
  execution to the outer Python frame, so warning `stacklevel` attribution can
  reach the importing source file. Module `getattr(..., default)` clears a caught
  module-level `__getattr__` `AttributeError` instead of leaking it.
  The lexer now preserves bracket depth before multiline triple-quoted strings
  inside joined list/tuple expressions, allowing the real `test.re_tests`
  corpus to import. The regex fallback tracks scoped verbose flags and removes
  comments/whitespace only while verbose mode is active.
  MSVC builds now enable the C++17 multiline regex flag, with explicit
  source-backed assertions for matches after newlines and scoped inline flags.
  Dynamic dataclass methods and classes created with `exec(..., globals())`
  retain their defining module, including across differing module-slot layouts.
  Union annotations preserve Python 3.14 `int | str` identity and behavior, and
  pickle validates the defining module before serializing global classes. The
  complete CPython 3.14 `test_dataclasses` package now passes all 280 tests with
  two intentional skips and no failures or errors.
  The complete CPython 3.14 `test_linecache` suite passes all 29 tests without
  skips, failures, or errors, including lazy loader caches and its concurrent
  read/write safety case.
  The complete CPython 3.14 `test_logging` suite passes all 274 tests with 19
  intentional platform skips and no failures or errors. Runtime dependency
  coverage now includes Windows multiprocessing spawn preparation and module
  remapping, dispatch-table reducers, concatenated pickle streams, inherited
  bytes/set primitives, SHA-224/SHA-256 through the internal `_sha2`
  accelerator, named-pipe handle transfer, message-preserving overlapped pipe
  readiness, multiprocessing queue feeder/listener shutdown, and append-mode
  file cursors used by rotating handlers. Public `logging`, `multiprocessing`,
  `hashlib`, and `hmac` behavior continues to come from the CPython 3.14 Python
  sources.
  The complete 35-class CPython 3.14 `test_inspect` inventory passes, with only
  explicit skips for CPython implementation details and optional native modules.
  All applicable traceback cases pass as well. Final regression evidence includes
  the source-retrieval, interactive-source, stdlib-source, predicate, module-main,
  and logical class-frame cases, with no failures or errors.

- [x] `os`, `os.path`, `ntpath`, `posixpath`, `pathlib`, `glob`, and `fnmatch`
  Coverage: real CPython 3.14 `os.py`, `ntpath.py`, `posixpath.py`,
  `pathlib`, `glob.py`, and `fnmatch.py` import from `C:/Python/Python314/Lib`;
  the `system_stdlib` section fixture covers VFS-backed file creation/removal,
  `os.stat_result`, `os.scandir`/`DirEntry`, path-like and bytes path basics,
  fd-level `os.open`/`write`/`lseek`/`read`/`fstat`/`close`, `dup`, `dup2`,
  `pipe`, `isatty`, and fd inheritability delegation,
  `os.environ` mapping writes, `putenv`/`unsetenv` interaction, and
  mapping-copy behavior through `copy()`, `dict(os.environ)`, and
  `dict.update(os.environ)`,
  `pathlib.Path` read/write/glob/rglob/match helpers, and `glob` root-dir,
  recursive, hidden-file, iterator, and bytes-path behavior. Runtime performance
  coverage includes near-zero-cost generic native-call dispatch when
  monitoring/profile hooks are disabled, plus `_sre` fast paths used by
  source-backed `fnmatch` cache callables.
  Windows runtime/VFS fixes cover UTF-8 and surrogate-preserving path transport,
  extended paths with significant trailing spaces, missing and invalid paths,
  junction creation/removal and reparse tags, `DirEntry.is_junction`, exact
  64-bit inode comparison for `samefile` and moves, nanosecond `utime`, and the
  descriptor/process primitives exercised by `os.py`. No public path module is
  implemented in C++.
  The complete available CPython 3.14 suites now pass with no failures or
  errors: `test_os` 369 tests with 172 intentional skips, `test_ntpath` 105
  tests with 20 skips, `test_posixpath` 94 tests with 29 skips, the full
  `test_pathlib` package 1,381 tests with 531 skips, `test_glob` 24 tests with
  three skips, and `test_fnmatch` 24 tests without skips.

- [x] `subprocess`, `_winapi`, `socket`, `select`, and `threading`
  Coverage: `_winapi`, `_socket`, `select`, and native thread foundations exist.
  CPython 3.14 `Lib/threading.py` now imports over XLang3's `_thread`
  primitives, and the system stdlib probe covers `current_thread`,
  `active_count`, `Thread.start`, target execution, `Thread.join`, `is_alive`,
  `ident`, `stack_size`, and `_thread._local` per-thread attribute isolation
  through `threading.local`, including subclass initial attribute isolation
  across worker/main threads and per-thread rerun of the subclass initializer
  with the original constructor arguments. It also covers CPython `Lock`, `RLock`,
  `RLock` private condition protocol, `Event`, and basic `Condition`
  ownership/notification over native synchronization primitives.
  CPython 3.14 `Lib/subprocess.py` and
  `Lib/socket.py` import from source; the process/socket probe covers
  `subprocess.run([sys.executable, "-c", ...], capture_output=True, text=True)`,
  `subprocess.run(..., env=...)` with both dict and `os.environ` mappings,
  basic `socket.socket` construction/timeout/close, loopback TCP bind/listen,
  getsockname/connect/accept/send/recv, empty `select.select`, and real
  `select.select` readability over sockets with original object return lists,
  loopback IPv4 `socket.getaddrinfo`, `socket.gethostbyname`, `inet_pton`,
  `inet_ntop`, and CPython `Lib/selectors.py` `SelectSelector` socketpair
  readiness. CPython `socket.create_connection` also succeeds against a
  loopback listener. CPython `http.client.HTTPConnection` now succeeds for a
  loopback GET using CPython `Lib/socket.py` file-object semantics over native
  `_socket.recv_into` and `_io.BufferedReader`, and socketpair `makefile()`
  reads through the same file-object stack.
  `subprocess` coverage now also includes `sys.executable -c`, pipe-backed
  `Popen.communicate(input=...)` over `sys.stdin.read()`, `check_call`
  seeing `sys.exit(0)` as process success, `CalledProcessError` preserving
  nonzero `SystemExit` status, `cwd`, `DEVNULL`, and timeout cleanup through
  CPython `Lib/subprocess.py`.
  Additional probes cover `socket.socketpair`, socket blocking/timeout state,
  `_overlapped` import foundation, CPython `Lib/asyncio` import, `_signal`
  `set_wakeup_fd`, and int-like signal enum arguments. Native datagram support
  now covers `sendto`/`recvfrom` with address tuples, flags, readiness, and
  timeout delegation, plus integer and buffer-returning `getsockopt` behavior
  for socket type/error options. `_thread.exit()` raises `SystemExit`, and
  `threading.local` subclass initialization replays positional and keyword
  constructor arguments independently in each thread. Existing section
  coverage also verifies thread profile-hook propagation into worker threads.
  Completion evidence from the CPython 3.14 tests includes `_winapi` 9/9;
  `subprocess.ProcessTestCase` 95 applicable passes plus 15 intentional skips,
  `RunFuncTestCase` 20 passes plus one skip, `Win32ProcessTestCase` 18 passes
  plus two skips, and `CommandsWithSpaces` 4/4. Threading evidence includes
  `ThreadJoinOnShutdown` two passes plus five platform skips, `AtexitTests` 3/3,
  `Barrier` 12/12, `Condition` 7/7, `ConditionAsRLock` 21 passes plus one skip,
  `ExceptHookTests` five passes plus one skip, and `LockTests` 16 passes plus
  one skip. The complete socket `GeneralModuleTests` class accounts for all 78
  cases with 46 passes and 32 explicit platform/optional-feature skips, with
  no failures or errors. Its final cases cover native socket type immutability,
  weak-proxy expiry, timely resource warnings, and source-level socket reprs.

- [x] `site`, `runpy`, `importlib`, `pkgutil`, and package metadata/resources
  Coverage: real CPython 3.14 `site.py`, `runpy.py`, `pkgutil.py`, and
  `importlib` package, including `importlib.metadata`, import from
  `C:/Python/Python314/Lib`; the
  `site_importlib` probe covers source-backed module paths,
  `importlib.import_module("math")`, core `pkgutil` APIs, catchable
  missing-module `ImportError.name`, `SourceFileLoader` filename/data/code
  behavior, `runpy.run_module`, `runpy.run_path`, `pkgutil.get_data`, and
  `importlib.resources.is_resource`/`read_text` over package data. Runtime
  coverage added for source loader metadata, file-loader `get_data` through VFS,
  `get_resource_reader` delegation to CPython `FileReader`, function
  `__annotate__`, lazy annotation basics, mapping-vs-dict-view detection,
  dict-subclass iteration, and module-level `__getattr__` fallback for lazy
  source-backed module attributes such as `typing.Match`. It now also covers
  `importlib.metadata.distributions(name=..., path=...)` discovering a real
  `.dist-info/METADATA` directory through CPython source code, backed by native
  `_frozen_importlib_external.PathFinder.find_distributions`, VFS path errors
  that raise catchable `FileNotFoundError`, live instance `__dict__` mapping
  storage, decorator-added bound-method attributes, IntEnum/int rich comparison,
  `mappingproxy` protocol delegation for Python-level mappings such as
  `OrderedDict`, descriptor-aware public `getattr`/`hasattr` behavior for
  Python properties, CPython `site.addsitedir` `.pth` path-line/import-line
  handling, and modern `importlib.resources.files()` traversal over
  file-backed packages, namespace packages through CPython `MultiplexedPath`,
  and zip packages through `zipimporter.get_resource_reader` plus CPython
  `ZipReader`. Native dependency coverage also includes `itertools.groupby`
  with keyword `key=` for CPython resource readers and receiver-MRO `super()`
  lookup for multiple-inheritance stdlib classes.
  Dictionary-subclass attribute storage is now separate from mapping entries
  in runtime and optimized VM attribute paths. Regression assertions cover
  overlapping attribute/item names, live `__dict__` additions, and
  `defaultdict.values()` excluding `default_factory` and instance attributes;
  this restores unfiltered `importlib.metadata.distributions()` discovery.
  CPython `importlib.reload()` now passes its optional target through native
  `_find_spec` and executes the returned source loader through `_exec`.
  `sys.path_hooks` retains both directory `FileFinder` and zipimport hooks, so
  CPython `pkgutil.iter_modules()` discovers ordinary source files while zip
  package imports continue to work.
  Completion evidence includes the mandatory 26-line `system_stdlib` fixture,
  which exercises `site`, `runpy`, `pkgutil`, ordinary packages, namespace
  packages, ZIP packages, reload, metadata discovery, and disk/ZIP resources
  entirely through the CPython source modules. Representative CPython 3.14
  classes pass without failures or skips: resource `OpenDiskTests` 5/5,
  resource `OpenZipTests` 5/5, metadata `BasicTests` 5/5, and metadata
  `DiscoveryTests` 3/3. Runtime regression coverage also verifies
  `ImportError(name=..., path=...)`, keyword construction of `ModuleSpec`,
  `BuiltinImporter.get_code`, and namespace-resource merging through live
  weak proxies used by the pure-Python `OrderedDict` implementation.

Validation (2026-09-11, MSVC 19.51, CPython 3.14.7): release build and
module-boundary check pass. All 47 CTest tests and the complete deterministic fixture suite pass,
including all core fixtures, every compatibility section, and all three
negative fixtures. The namespace-resource fixture now creates its expected
cache directory explicitly instead of depending on prior CPython execution.
The complete CPython `test_re.py` suite passes all 165 top-level tests with 14
intentional skips and no failures or errors. All eight system-stdlib rows are
complete under the source-backed compatibility criteria above.
