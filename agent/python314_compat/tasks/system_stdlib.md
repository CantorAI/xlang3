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

- [~] `textwrap`, `_colorize`, `traceback`, `inspect`, `dataclasses`, `linecache`, and `logging`
  Coverage: real CPython 3.14 source modules import from `C:/Python/Python314/Lib`
  and the `system_stdlib` section fixture covers source-backed module paths,
  `inspect.signature(object)`, dataclass construction, `_colorize.can_colorize`,
  and `logging.getLogger`. Runtime/native primitive fixes added for `_tokenize`,
  f-string embedded-expression lexing, annotation storage, tuple subclass
  construction, type/member descriptor lookup, memoryview item sizing, `nt`
  terminal-color probes, bytearray/bytes concatenation, `_sre` character class
  translation, CPython `inspect.py` `CO_*` flag generation through live
  `globals()`, disabled `sys.monitoring` near-zero-cost frame handling, real
  `mappingproxy` construction/iteration/views for `Signature.parameters` and
  class dictionaries, CPython-shaped `f_lasti`/`tb_lasti`/`co_lines` offsets
  for `traceback.py`, `co_positions()` statement-level line/column metadata,
  catchable `ZeroDivisionError`, keyword-compatible `bytes.decode`, dataclass
  frozen/default_factory/slots/inheritance/InitVar/ClassVar basics, correct
  dynamic `exec(..., globals())` module-slot isolation, and logging exception
  formatter output.
  Remaining: full regex semantics, expression-exact traceback column/end-position
  metadata, complete dataclass generated-method/field-order edge semantics, and
  deeper logging handler/filter/error behavior remain pending.

- [~] `os`, `os.path`, `ntpath`, `posixpath`, `pathlib`, `glob`, and `fnmatch`
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
  recursive, hidden-file, iterator, and bytes-path basics.
  Remaining: this path is correct but too slow for the full fixture, especially
  `pathlib`/`glob`/`fnmatch`; continue by fixing runtime/VFS/path protocol
  primitives and import/runtime hot paths instead of restoring `os.path`,
  `pathlib`, `glob`, or `fnmatch` C++ facades.

- [~] `subprocess`, `_winapi`, `socket`, `select`, and `threading`
  Coverage: `_winapi`, `_socket`, `select`, and native thread foundations exist.
  CPython 3.14 `Lib/threading.py` now imports over XLang3's `_thread`
  primitives, and the system stdlib probe covers `current_thread`,
  `active_count`, `Thread.start`, target execution, `Thread.join`, `is_alive`,
  `ident`, `stack_size`, and `_thread._local` per-thread attribute isolation
  through `threading.local`. It also covers CPython `Lock`, `RLock`,
  `RLock` private condition protocol, `Event`, and basic `Condition`
  ownership/notification over native synchronization primitives.
  CPython 3.14 `Lib/subprocess.py` and
  `Lib/socket.py` import from source; the process/socket probe covers
  `subprocess.run([sys.executable, "-c", ...], capture_output=True, text=True)`,
  `subprocess.run(..., env=...)` with both dict and `os.environ` mappings,
  basic `socket.socket` construction/timeout/close, loopback TCP bind/listen,
  getsockname/connect/accept/send/recv, empty `select.select`, and real
  `select.select` readability over sockets with original object return lists,
  plus loopback IPv4 `socket.getaddrinfo` and CPython `Lib/selectors.py`
  `SelectSelector` socketpair readiness.
  Additional probes cover `socket.socketpair`, socket blocking/timeout state,
  `_overlapped` import foundation, CPython `Lib/asyncio` import, `_signal`
  `set_wakeup_fd`, and int-like signal enum arguments.
  Remaining: complete truthful process/socket primitives for broader
  `subprocess` and networking, exact daemon/shutdown lifecycle, deeper `_thread._local`
  subclass/reinitialization edge cases, deeper condition/lock edge cases,
  profile/trace propagation parity, and import-time/runtime performance.

- [~] `site`, `runpy`, `importlib`, `pkgutil`, and package metadata/resources
  Coverage: real CPython 3.14 `site.py`, `runpy.py`, `pkgutil.py`, and
  `importlib` package import from `C:/Python/Python314/Lib`; the
  `site_importlib` probe covers source-backed module paths,
  `importlib.import_module("math")`, core `pkgutil` APIs, catchable
  missing-module `ImportError.name`, `SourceFileLoader` filename/data/code
  behavior, `runpy.run_module`, `runpy.run_path`, `pkgutil.get_data`, and
  `importlib.resources.is_resource`/`read_text` over package data. Runtime
  coverage added for source loader metadata, file-loader `get_data` through VFS,
  `get_resource_reader` delegation to CPython `FileReader`, function
  `__annotate__`, lazy annotation basics, mapping-vs-dict-view detection, and
  dict-subclass iteration.
  Remaining: package metadata, startup-site `.pth` handling, exact importlib
  lock/cache semantics, namespace/zip resource discovery, and import-time
  performance.
