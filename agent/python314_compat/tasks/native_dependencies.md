# Native Dependency Tasks

Validation baseline: after the native dependency updates documented below, the
full fixture runner passed all core and compatibility sections and Release CTest
passed 47/47 tests, including the aggregate fixture suite.

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
  and buffering flags, and `TextIOWrapper.buffer` exposes the wrapped binary stream.
  `TextIOWrapper.name` forwards a wrapped stream name. `FileIO` now constructs
  descriptor-backed unbuffered binary streams through the runtime open path,
  with mode normalization, keyword arguments, `closefd`/opener forwarding, and
  raw read/write/seek/close behavior. Buffered raw-stream wrappers now expose
  `read1`, `readinto`, `readinto1`, and non-consuming `peek()` for seekable
  raw streams over the same native data path. In-memory streams expose the
  standard non-terminal `isatty()` result and retain closed-stream validation.
  `BytesIO.getbuffer()` now retains a writable live view whose changes are
  observed by direct stream reads, `readinto()`, seek operations, and
  `getvalue()` calls. Active buffer exports prevent `BytesIO` writes,
  truncation, and close until the memoryview is released.
  Native `FileIO.readinto()` fills writable byte buffers through the same
  descriptor-backed file path as `read()`.
  Unbuffered binary `FileIO.readall()` is available on the descriptor-backed
  native object.
  `BytesIO.readinto1()` now exposes the raw single-buffer read contract.
  Remaining: full TextIOWrapper, BufferedIOBase, FileIO, and exact errors.

- [~] _socket, select, and _signal
  Coverage: `tests/fixtures/core/socket_select_modules.py`,
  `tests/fixtures/probes/system_stdlib/socketpair_probe.py`, and
  `tests/fixtures/probes/system_stdlib/asyncio_probe.py`.
  `tests/fixtures/compat_sections/standard_modules.py` also covers loopback
  TCP bind/listen/getsockname/connect/accept/send/recv, timeout connect wait,
  `select.select` socket readability with original object return lists, and
  OS-backed IPv4/IPv6 `getaddrinfo`, hostname lookup, and IPv6 numeric address conversion. The core socket fixture covers loopback UDP
  `sendto`, `recvfrom`, and `recvfrom_into`, plus `socketpair` write shutdown.
  CPython `Lib/selectors.py` now runs a
  `SelectSelector` socketpair readiness path over these primitives.
  `_socket.socket.recv_into` writes into writable bytearray and memoryview
  buffers, `TCP_NODELAY` is exported, and native file-like socket helpers were
  removed from `_socket.socket` so CPython `Lib/socket.py` owns
  `socket.socket.makefile` as in CPython.
  `_overlapped` now keeps native overlapped address state and an IOCP completion
  queue/fallback for immediate and cancelled operations, enough for CPython
  `asyncio.run()` startup/shutdown over the Windows proactor path.
  Numeric IPv6 `getnameinfo()` now accepts the CPython four-element sockaddr
  form alongside IPv4 socket addresses; numeric IPv4 `inet_aton`/`inet_ntoa`
  conversion is covered directly.
  Native IPv6 sockets now carry `sockaddr_in6` through loopback UDP/TCP
  bind, connect, send/receive, accept, and local/peer names, using CPython's
  four-element IPv6 sockaddr tuples; `has_ipv6` now reflects the native stack.
  Native `gethostbyname_ex()` and `gethostbyaddr()` return CPython-compatible
  hostname, aliases, and IPv4 address tuples through Winsock resolution.
  `getprotobyname()` and service-name/port resolution use Winsock for
  source-backed `socket` callers.
  Network byte-order helpers reject negative and oversized values with CPython
  overflow errors.
  Socket receive operations accept CPython flags, including non-consuming
  `MSG_PEEK` reads; `recv_into()` supports both nbytes and flags for writable
  byte buffers.
  `send()` and `sendall()` forward optional socket flags to Winsock.
  Validation update: all three CPython 3.14 Windows signal tests pass, including
  `SIGBREAK`, invalid-signal errors, handler reset, and subprocess
  `KeyboardInterrupt` exit behavior.
  Remaining: broader address-family/service resolution, deeper selectors edge
  behavior, signal delivery, full
  `_overlapped` IOCP behavior, and platform constants.

- [~] _weakref and _collections
  Coverage: `tests/fixtures/core/weakref_module.py` covers reference and proxy
  lookup, live-reference equality, weak-reference enumeration, collection-time reference expiration,
  CPython weak-reference hash caching and dead-reference hash errors,
  callback-free reference reuse for the same live referent,
  delegated proxy attribute mutation plus `len`, iteration, indexing, and
  membership, truth-value, and equality forwarding, and callback delivery through `gc.collect()` for both
  references and proxies, including last-created-first callback order and
  callable-proxy invocation;
  `tests/fixtures/core/collections_queue_modules.py` covers the native collection
  dependency surface used by source-backed `collections`, including deque
  rotation, reversal, positional lookup with bounded index searches, insertion, bounded representation, and
  independent `copy()` results. CPython 3.14 focused `TestBasic` coverage also
  passes deque comparisons, concatenation, in-place concatenation, and in-place
  repetition. Native deque reducers support source-backed `copy.copy()` and
  protocol-4 pickle round-trips; native defaultdict reducers preserve both
  mapping entries and default factories through copy and protocol-4 pickle,
  and `|` / `|=` preserve defaultdict type and factory.
  Deque search operations honor user-defined equality and reject mutation during
  comparison, matching the CPython container safety contract.
  Native deque integer-taking APIs accept the full `__index__` protocol for
  construction, positional access and mutation, search bounds, rotation,
  insertion, and repetition.
  Validation: focused CPython 3.14 `test_deque.TestBasic` cases pass for copy,
  pickle, comparisons, concatenation, and in-place operations.
  Remaining: full weakref callback lifecycle timing and proxy parity; deque operation,
  iterator, comparison, copy/pickle, and representation parity; plus
  defaultdict/OrderedDict parity. CPython 3.14 `test_deque.TestBasic` currently
  has 24 errors and 8 failures across 48 tests.

- [~] zlib and zipimport
  Coverage: `tests/fixtures/core/zlib_module.py`, `tests/fixtures/core/zipfile_module.py`, `tests/fixtures/core/zipimport_module.py`, `tests/fixtures/core/sys_path_importer_cache.py`.
  Validation: CPython 3.14 `test_zipimport` focused checks pass for bad archives,
  source/bytecode selection, nested package prefixes, direct member data, and
  cache invalidation. The core importer fixture also exercises `find_spec` and
  `get_filename` directly. Native zlib streaming tracks `eof`, `unconsumed_tail`,
  and accumulated `unused_data` after a stream ends; compressor and decompressor
  objects can be copied while their streams are active, including configured
  preset dictionaries; gzip-wrapped streaming is covered through `wbits`.
  Terminal compressor state now reports `zlib.error` for repeat flushes,
  invalid flush modes, and writes after `Z_FINISH`, matching CPython
  stream-finalization behavior.
  Bounded decompression now retains pending compressed input safely so `flush()`
  drains the remaining decoded stream, including on a copied decompressor.
  One-shot `decompress()` validates `wbits` and `bufsize` argument types and
  rejects negative buffer sizes with CPython-compatible errors.
  Validation: selected CPython 3.14 `test_zlib.CompressObjectTestCase` cases
  pass for dictionary compression, compressor/decompressor copies, incremental
  decompression, maximum output lengths, and flush modes.
  Remaining: full compression matrix, encrypted ZIP behavior deferred, hash-based
  bytecode validation modes, and remaining import edge cases.

- [~] _pickle and marshal
  Coverage: `tests/fixtures/core/sys_structseq_pickle.py` and
  `tests/fixtures/core/pickle_module.py`, including native `PickleBuffer.raw()`
  and release lifetime semantics, protocol-5 out-of-band buffers, and native
  Pickler/Unpickler persistent-ID hooks through source-backed `pickle`. Native
  Pickler and Unpickler instances retain their source-backed delegates across
  repeated dump/load calls, preserving stream memo identity; `Pickler.clear_memo()`
  resets that memo for the next record, and `Pickler.memo` / `Unpickler.memo`
  expose their live source-backed memos before or after protocol execution.
  Validation: 62 focused CPython 3.14 `test_marshal` cases pass for scalars,
  containers, errors, byte buffers, code objects, compatibility, interning, and
  slices (with two platform skips).
  `marshal.dumps()` rejects non-integer version arguments with `TypeError`.
  Remaining: full pickle protocol compatibility, extension codes, and
  remaining marshal stress/C-API cases.
