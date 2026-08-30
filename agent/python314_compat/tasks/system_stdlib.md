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
  `weakref.ref` plus `WeakKeyDictionary` behavior. Runtime coverage added for
  structural list comparison so source-backed collections tests use normal
  Python equality rather than object identity.

- [ ] `json`, `pickle`, `copy`, and `copyreg`
  Coverage: `_pickle`, `marshal`, and `_struct` native dependencies exist.
  Remaining: run CPython pure modules on top of runtime/native support; do not
  revive native `json` or public `pickle` facades.

- [ ] `traceback`, `inspect`, `linecache`, and `logging`
  Coverage: frame/debug foundations exist.
  Remaining: complete real code/frame/source/traceback objects so these Python
  modules work from CPython `Lib`.

- [ ] `os`, `os.path`, `ntpath`, `posixpath`, `pathlib`, `glob`, and `fnmatch`
  Coverage: native `nt`/`posix`, `_stat`, VFS, and file basics exist.
  Remaining: fill OS/VFS/path protocol gaps required by the real Python modules
  instead of restoring `os.path` or `pathlib` C++ facades.

- [ ] `subprocess`, `_winapi`, `socket`, `select`, and `threading`
  Coverage: `_winapi`, `_socket`, `select`, and native thread foundations exist.
  Remaining: make CPython system/process/thread libraries run against truthful
  native primitives, including XLang3 extensions only through explicit keyword
  options such as future shared-memory transport.

- [ ] `site`, `runpy`, `importlib`, `pkgutil`, and package metadata/resources
  Coverage: import bootstrap native modules exist and CPython source imports
  are preferred over native fallback.
  Remaining: finish source loader/package/resource behavior needed by real
  Python startup and package-discovery modules.
