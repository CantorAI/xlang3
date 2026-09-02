# Native Dependency Tasks

- [x] errno
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: none for the current dependency surface.

- [x] _thread subset
  Coverage: current `_thread` smoke coverage; full CPython `Lib/threading.py`
  coverage remains in `async_threads.md`.
  Remaining: none for the current subset.

- [~] _winapi
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`
  Additional coverage: XLang3 pseudo handles from native dependency shims can
  be closed without calling the Windows kernel handle table.
  Additional coverage: `CreateProcess` accepts explicit environment mappings,
  including plain dicts and CPython `os.environ` mapping objects, and passes a
  Unicode environment block to the child process.
  Remaining: deeper process, handle, wait, pipe, and detailed Windows error surfaces.

- [~] _stat and os stat structures
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: complete mode constants and all stat result edge cases.

- [~] _io
  Coverage: `tests/fixtures/core/io_module_streams.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: full TextIOWrapper, BufferedIOBase, FileIO, StringIO/BytesIO, detach/reconfigure, and exact errors.

- [~] _socket, select, and _signal
  Coverage: `tests/fixtures/core/socket_select_modules.py`,
  `tests/fixtures/probes/system_stdlib/socketpair_probe.py`, and
  `tests/fixtures/probes/system_stdlib/asyncio_probe.py`.
  `tests/fixtures/compat_sections/standard_modules.py` also covers loopback
  TCP bind/listen/getsockname/connect/accept/send/recv, timeout connect wait,
  `select.select` socket readability with original object return lists, and
  OS-backed IPv4 `getaddrinfo`.
  `_overlapped` now keeps native overlapped address state and an IOCP completion
  queue/fallback for immediate and cancelled operations, enough for CPython
  `asyncio.run()` startup/shutdown over the Windows proactor path.
  Remaining: broader address-family/service resolution, selectors module integration, signal delivery, full
  `_overlapped` IOCP behavior, and platform constants.

- [~] _weakref and _collections
  Coverage: `tests/fixtures/core/weakref_module.py`, `tests/fixtures/core/collections_queue_modules.py`
  Remaining: lifecycle cleanup, proxy behavior, deque/defaultdict/OrderedDict parity.

- [~] zlib and zipimport
  Coverage: `tests/fixtures/core/zlib_module.py`, `tests/fixtures/core/zipfile_module.py`, `tests/fixtures/core/sys_path_importer_cache.py`
  Remaining: full compression matrix, encrypted ZIP behavior deferred, and import edge cases.

- [~] _pickle and marshal
  Coverage: `tests/fixtures/core/sys_structseq_pickle.py`
  Remaining: full protocol compatibility, recursive object graphs, persistent ids, extension codes, and marshal code-object parity.
