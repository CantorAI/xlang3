# Standard Module Tasks

- [x] remove pure-stdlib C++ facade dependency
  Coverage: pure facade sources removed; mixed module registrations now keep only native dependency modules; CPython `Lib/*.py` probes confirm public `ast`, `string`, and `opcode` load from the Python 3.14 library path.
  Remaining: none for facade removal. Continue failures as runtime/native dependency gaps, not as new C++ facades.

- [x] os and nt/posix
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`
  Coverage update: `tests/fixtures/compat_sections/system_stdlib.py` covers
  CPython `Lib/os.py` delegating `open`, `write`, `lseek`, `read`, `fstat`,
  `close`, `dup`, `dup2`, `pipe`, `isatty`, `get_inheritable`, and
  `set_inheritable` through the native `nt`/`posix` dependency module, plus
  CPython-compatible `os.environ` mapping writes, `putenv`/`unsetenv`
  interaction, and mapping copy/update through the runtime `dict` protocol.
  Coverage update: `tests/fixtures/core/os_process_windows.py` runs CPython's
  source-backed `Lib/os.py` over native `nt` for quoted `spawnv`, asynchronous
  `waitpid`, `waitstatus_to_exitcode`, explicit `spawnve` environments,
  `system`, `startfile`, and `kill`. Native failures carry CPython-shaped
  `errno`, `winerror`, `strerror`, and `filename` state.
  Remaining: none for the scoped Windows process-helper and error-mapping row.

- [x] os.path, pathlib, stat, glob, fnmatch
  Coverage: `tests/fixtures/core/logging_pathlib_modules.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Coverage update: CPython 3.14's complete `test_ntpath` (105 tests),
  `test_fnmatch` (24), `test_glob` (24), `test_stat` (22), and `test_pathlib`
  (1,381) suites pass with their platform skips. The pathlib run exposed and
  now covers inherited default object representation for virtual path error
  messages; `tests/fixtures/core/default_object_repr.py` preserves the runtime
  fix alongside the existing real scandir/stat/path fixtures.
  Remaining: none for the scoped normalization, Windows drive/UNC,
  scandir/stat, and path-like integration row.

- [x] sys
  Coverage: `tests/fixtures/compat_sections/standard_modules.py` plus focused `tests/fixtures/core/sys_*.py`
  Coverage update: 99 applicable tests from CPython 3.14's `test.test_sys`
  pass, together with the focused startup, stdio, struct-sequence, intern,
  monitoring-event, profile/trace, coroutine-origin, and command-path fixtures.
  This includes live cross-thread frames/exceptions, canonical frame/code identity,
  finalization state, interactive and command `SystemExit`, `sys.tracebacklimit`,
  stdio encoding/error policy, executable/original argv, and `_stdlib_dir`.
  CPython's single `_testcapi`/`_testinternalcapi` JIT harness is excluded because
  those are CPython implementation-test extension modules; XLang3's truthful
  external-JIT state is covered directly.
  Remaining: none for the scoped public `sys` API and XLang3 runtime state.

- [x] time
  Coverage: `tests/fixtures/core/time_module.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Coverage update: CPython 3.14's complete `test.test_time` suite passes (64
  tests, 23 platform/private-C skips). Coverage includes platform timezone and
  DST fields, wide and negative years, tuple bounds/defaults, embedded NUL and
  Unicode formats, warning/error context, protocol-0 `struct_time` restoration,
  clock families, timestamp conversions, and parsing directives.
  Remaining: none for the scoped public `time` API on Windows.

- [x] abc and _abc
  Coverage: `tests/fixtures/core/abc_module_metadata.py`, `tests/fixtures/core/abc_runtime.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Coverage update: CPython 3.14's complete `test.test_abc` suite passes (72
  tests) for both source-backed `_py_abc.ABCMeta` and `Lib/abc.py` over the
  native `_abc` dependency. Coverage includes descriptor subclasses, cloned
  abstract properties, transitive virtual registration and invalidation,
  hostile `__subclasses__` results, class-keyword forwarding, exact abstract
  instantiation diagnostics, and legacy abstract descriptors.
  Remaining: none for the scoped public ABC API and native cache/registry behavior.

- [x] importlib, pkgutil, runpy, site
  Coverage: `tests/fixtures/core/importlib_module.py`, `tests/fixtures/core/runpy_module.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Coverage update: CPython 3.14's complete `test_runpy` (40 tests),
  `test_pkgutil` (21 tests), and applicable `test_site` cases (43 passed, 4
  platform skips) pass. `test_importlib.test_namespace_pkgs` passes all 30
  filesystem, ZIP, dynamic-path, invalidation, precedence, and reload cases;
  `test_importlib.resources.test_files` passes all 29 disk, ZIP, namespace,
  implicit-caller, path-like, and compiled-only cases. The threaded import
  suite passes all four runnable circular/race/side-effect cases with five
  resource-limit skips. Focused importlib API coverage passes meta-path
  `find_spec`, loader replacement, built-in reload, and finder/cache
  invalidation behavior. Public modules load from CPython 3.14 `Lib`, while
  C++ remains confined to bootstrap/runtime primitives and native loaders.
  Remaining: none for the scoped import, execution, site startup, namespace,
  resource-reader, and concurrent-import behavior.

- [x] codecs, locale, string, tokenize
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`,
  `tests/fixtures/core/tokenize_runtime.py`, and
  `tests/fixtures/core/locale_runtime.py`.
  Coverage update: CPython 3.14's 129 deterministic `test_tokenize` cases pass,
  including all 17 native-tokenizer cases; a completed random-file roundtrip
  pass and exact large-source token/byte comparisons cover `argparse`, f-string,
  traceback, types, and Unicode-identifier sources. CPython 3.14's complete
  `test_locale` passes (43 tests, 10 platform/unsupported-locale skips) through
  source-backed `Lib/locale.py` over the native `_locale` dependency. The
  The complete scoped CPython 3.14 codec suite passes as 40 independently
  isolated classes covering 287 discovered tests with zero failing classes;
  the only skips are the documented `_ctypes`, `_testinternalcapi`, locale, and
  CPython-internal exclusions. This includes exact UTF-7 modified-Base64,
  UTF-8/UTF-8-SIG, UTF-16/LE/BE/ex, UTF-32/LE/BE, Unicode-escape and raw-
  Unicode-escape, IDNA/nameprep/punycode, charmap, transform, surrogate,
  registry/cache, copy/pickle, exception-note, stream, incremental, and error-
  handler behavior. The checked-in tokenize and locale runtime fixtures and
  the complete standard-modules fixture pass in Release. Public codec
  discovery, stream classes, locale, string, and tokenization remain sourced
  from CPython 3.14 `Lib`; native code is confined to `_codecs`, `_locale`,
  tokenizer/runtime primitives, and legitimate private native dependencies.
  Remaining: none for the scoped codec, locale, string, and tokenize behavior.

- [x] json, pickle, marshal, struct
  Coverage: `tests/fixtures/core/json_module.py`, `tests/fixtures/core/sys_structseq_pickle.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Coverage update: public `json`, `pickle`, and `struct` load from CPython
  3.14 `Lib`; C++ supplies only `_json`, `_pickle`, `_struct`, `marshal`, and
  runtime protocols. JSON decoding/encoding and stream APIs, marshal stream and
  value round trips, struct format/pack/unpack/buffer/iterator behavior, and
  pickle protocols 0 through 5 are covered. Pickle validation includes the
  public and private module APIs, pure-Python pickler/unpickler/error classes,
  recursive containers, globals, extension codes, out-of-band buffers,
  `NEWOBJ`/`NEWOBJ_EX`, singleton and exception reduction, bound methods,
  builtin subclasses, and large/chunked container reproductions.
  Remaining: none for the scoped serialization APIs.

- [x] argparse, ast, code, dis, enum, dataclasses, typing, numbers
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`
  Coverage update: all public modules in this row load from CPython 3.14
  `Lib`. The complete CPython `test_dataclasses` package passes all 280 tests
  with two intentional skips. The deterministic section covers argparse
  parsing and diagnostics, AST parsing/dumping, code compilation, disassembly
  over live code metadata, enum and flag operations, typing aliases and
  annotations, and numeric ABC registration through runtime primitives and
  legitimate private native dependencies.
  Remaining: none for the scoped source-backed behavior.

- [x] functools, itertools, operator, collections, queue
  Coverage: `tests/fixtures/core/collections_queue_modules.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Coverage update: public `functools`, `operator`, `collections`, and `queue`
  load from CPython 3.14 `Lib`; `itertools` and the underscore modules remain
  legitimate native dependencies. Coverage includes wrapper metadata,
  partials, comparison keys, reduction and cache behavior; operator item,
  attribute, method and in-place helpers; collection mappings, counters,
  deques and default dictionaries; bounded and simple queue semantics; and
  lazy iterator combinators, grouping, tee, pairwise, batching, products,
  combinations, permutations, accumulation, compression and zip-longest.
  Remaining: none for the scoped helper and iterator behavior.

- [x] traceback, inspect, linecache, logging, warnings
  Coverage: `tests/fixtures/core/traceback_module.py`, `tests/fixtures/core/inspect_module.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Coverage update: every public module loads from CPython 3.14 `Lib`. The
  complete CPython `test_linecache` suite passes all 29 tests, `test_logging`
  passes all 274 tests with 19 intentional platform skips, and the complete
  35-class applicable `test_inspect` inventory passes with explicit
  CPython-internal/optional-module skips. Applicable traceback cases and the
  focused fixtures cover live frames, code lines and positions, source lookup,
  exception chains, formatting, warning filters, recording and stack levels.
  Remaining: none for the scoped diagnostics and source integration.

- [x] socket, subprocess, winreg, urllib.parse, xmlrpc, http
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`
  Coverage update: subprocess now covers explicit environment mappings through
  `_winapi.CreateProcess`.
  Coverage update: socket now covers real loopback stream bind/listen/connect,
  accept, send, recv, close, and timeout connect handling through CPython
  `Lib/socket.py` over native `_socket`.
  Coverage update: `select.select` now performs native socket readiness and
  returns the original Python objects in ready lists.
  Coverage update: `socket.getaddrinfo` now resolves loopback IPv4 addresses
  through native `_socket` and returns CPython-shaped tuples.
  Coverage update: CPython `Lib/selectors.py` now covers `SelectSelector`
  registration and readable socketpair dispatch.
  Coverage update: CPython `socket.create_connection` now succeeds against a
  loopback listener using native `_socket` resolver/connect primitives.
  Coverage update: CPython `http.client.HTTPConnection` now performs a loopback
  GET over CPython `Lib/socket.py`, native `_socket.recv_into`, `_io.BufferedReader`,
  `email.parser`, and native regex octal escape support.
  Coverage update: public `socket`, `subprocess`, `urllib.parse`, `xmlrpc`, and
  `http` load from CPython 3.14 `Lib`, over native `_socket`, `select`,
  `_winapi`, and `winreg`. Completion evidence includes all 78 socket
  `GeneralModuleTests` cases (46 passes and 32 explicit platform/feature
  skips), 137 applicable subprocess cases across process/run/Windows/quoted-
  path classes, and deterministic URL parsing/quoting/query, XML-RPC codec,
  HTTP status/client, registry constants, selector, loopback socket and
  loopback HTTP behavior.
  Remaining: none for the scoped Windows networking, process, registry, URL,
  XML-RPC, and HTTP behavior.
