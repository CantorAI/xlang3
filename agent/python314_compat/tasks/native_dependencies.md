# Native Dependency Tasks

- [x] errno
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: none for the current dependency surface.

- [x] _thread subset
  Coverage: current `_thread` smoke coverage; full CPython `Lib/threading.py`
  coverage remains in `async_threads.md`.
  Remaining: none for the current subset.

- [x] _winapi
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`; XLang3 pseudo
  handles can be closed without calling the Windows kernel handle table; and
  `CreateProcess` accepts plain-dict and CPython `os.environ` mappings through a
  Unicode environment block.
  Validation: all nine CPython 3.14 `Lib/test/test_winapi.py` cases pass under
  XLang3, including waits over 3,969 events, invalid-handle errors, pathname
  conversion, and named-pipe read/write/peek behavior.
  Remaining: none for the supported Windows `_winapi` surface.

- [x] _stat and os stat structures
  Coverage: `tests/fixtures/core/imp_stat_modules.py` covers stat tuple
  indexes, mode masks, permission aliases, special bits, and the Windows
  stub file-type predicates; `standard_modules.py` exercises `os.stat_result`
  through source-backed `os`, `pathlib`, and directory-entry paths.
  Validation: CPython 3.14 `Lib/test/test_stat.py` passes under XLang3
  (22 tests, with 14 platform-skipped cases).
  Remaining: none for the supported Windows stat surface.

- [~] _io
  Coverage: `tests/fixtures/core/io_module_streams.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Coverage update: native `_io` now supports StringIO/BytesIO keyword construction,
  truncate, IOBase closed/readable/writable guards, and buffered wrapper delegation
  over raw `readinto` streams so CPython `socket.py`, `email.parser`, and
  `http.client` can use normal stdlib file-object paths. `TextIOWrapper.detach()`
  returns its wrapped binary buffer and invalidates the wrapper as CPython does;
  `TextIOWrapper.reconfigure()` updates encoding, error handling, newline mode,
  and buffering flags.
  Remaining: full TextIOWrapper, BufferedIOBase, FileIO, and exact errors.

- [~] _socket, select, and _signal
  Coverage: `tests/fixtures/core/socket_select_modules.py`,
  `tests/fixtures/probes/system_stdlib/socketpair_probe.py`, and
  `tests/fixtures/probes/system_stdlib/asyncio_probe.py`.
  `tests/fixtures/compat_sections/standard_modules.py` also covers loopback
  TCP bind/listen/getsockname/connect/accept/send/recv, timeout connect wait,
  `select.select` socket readability with original object return lists, and
  OS-backed IPv4 `getaddrinfo`. CPython `Lib/selectors.py` now runs a
  `SelectSelector` socketpair readiness path over these primitives.
  `_socket.socket.recv_into` writes into writable bytearray and memoryview
  buffers, `TCP_NODELAY` is exported, and native file-like socket helpers were
  removed from `_socket.socket` so CPython `Lib/socket.py` owns
  `socket.socket.makefile` as in CPython.
  `_overlapped` now keeps native overlapped address state and an IOCP completion
  queue/fallback for immediate and cancelled operations, enough for CPython
  `asyncio.run()` startup/shutdown over the Windows proactor path.
  Validation update: all three CPython 3.14 Windows signal tests pass, including
  `SIGBREAK`, invalid-signal errors, handler reset, and subprocess
  `KeyboardInterrupt` exit behavior.
  Remaining: broader address-family/service resolution, deeper selectors edge
  behavior, signal delivery, full
  `_overlapped` IOCP behavior, and platform constants.

- [~] _weakref and _collections
  Coverage: `tests/fixtures/core/weakref_module.py` covers reference and proxy
  lookup, live-reference equality, weak-reference enumeration, and collection-time reference expiration;
  `tests/fixtures/core/collections_queue_modules.py` covers the native collection
  dependency surface used by source-backed `collections`, including deque
  rotation, reversal, positional lookup, insertion, bounded representation, and
  independent `copy()` results. CPython 3.14 focused `TestBasic` coverage also
  passes deque comparisons, concatenation, in-place concatenation, and in-place
  repetition. Native deque reducers support source-backed `copy.copy()` and
  protocol-4 pickle round-trips.
  Remaining: weakref callback lifecycle and proxy parity; deque operation,
  iterator, comparison, copy/pickle, and representation parity; plus
  defaultdict/OrderedDict parity. CPython 3.14 `test_deque.TestBasic` currently
  has 24 errors and 8 failures across 48 tests.

- [~] zlib and zipimport
  Coverage: `tests/fixtures/core/zlib_module.py`, `tests/fixtures/core/zipfile_module.py`, `tests/fixtures/core/zipimport_module.py`, `tests/fixtures/core/sys_path_importer_cache.py`.
  Validation: CPython 3.14 `test_zipimport` focused checks pass for bad archives,
  source/bytecode selection, nested package prefixes, direct member data, and
  cache invalidation. Native zlib streaming tracks `eof`, `unconsumed_tail`,
  and accumulated `unused_data` after a stream ends.
  Remaining: full compression matrix, encrypted ZIP behavior deferred, hash-based
  bytecode validation modes, and remaining import edge cases.

- [~] _pickle and marshal
  Coverage: `tests/fixtures/core/sys_structseq_pickle.py` and
  `tests/fixtures/core/pickle_module.py`, including native `PickleBuffer.raw()`
  and release lifetime semantics plus protocol-5 round-tripping through
  source-backed `pickle`.
  Validation: 62 focused CPython 3.14 `test_marshal` cases pass for scalars,
  containers, errors, byte buffers, code objects, compatibility, interning, and
  slices (with two platform skips).
  Remaining: full pickle protocol compatibility, recursive object graphs,
  persistent ids, extension codes, and remaining marshal stress/C-API cases.
