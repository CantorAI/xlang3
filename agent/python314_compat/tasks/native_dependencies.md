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
  `_thread.start_new_thread()` accepts CPython's optional third positional
  `kwargs` mapping when it is empty, which is the native scheduling form used
  by the CPython Windows regrtest load tracker.
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
  `StringIO` now honors its `newline` construction mode, including universal
  newline normalization, line iteration delimiters, and the CPython `newlines`
  reporting property. `TextIOWrapper.newlines` likewise reports newline forms
  observed through reads.
  Buffered binary wrappers now forward writes and flushes to their wrapped raw
  streams, instead of taking the text-wrapper encoding path, and share their
  wrapped stream's seek, tell, and truncate state.
  Buffered reader, writer, and random wrappers expose their wrapped `raw`
  stream with closed-wrapper validation.
  `BufferedRWPair` now preserves both endpoints, routes reads and writes to
  the appropriate stream, exposes `reader` / `writer` properties, and covers
  `read1`, `readinto`, `readinto1`, and inherited `detach()` behavior.
  Buffered reader, writer, and random wrappers now support `detach()`,
  returning their raw stream while invalidating the wrapper.
  In-memory `BytesIO` and `StringIO` expose `fileno()` and raise the standard
  `UnsupportedOperation` result.
  The interpreter's native console adapters now inherit the actual `_io`
  `TextIOWrapper`, `BufferedReader`, and `BufferedWriter` classes while
  retaining their console handles; this lets source-backed CPython setup code
  use `isinstance(sys.stdout, io.TextIOWrapper)` and `reconfigure()` normally.
  Descriptor-backed `FileIO.readinto()` accepts writable `array.array` buffer
  exporters as well as bytearray and memoryview values, and its invalid seek
  and readinto calls raise `TypeError` rather than an internal runtime error.
  Focused CPython 3.14 `test_io.CIOTest.test_raw_file_io` passes after normal
  module setup. Focused `CIOTest.test_buffered_file_io`, `test_readline`,
  `test_invalid_operations`, and `test_with_open` also pass, including
  `readinto1()`, `readline(None)`, writable array exporters, and the
  `UnsupportedOperation` contracts for unavailable read/write/seek actions.
  Private native binary streams and descriptor-backed files now accept the
  runtime's writable `array.array` buffer exporters for both reads and writes.
  Filename-based opens correctly reject `closefd=False`, preserving the
  descriptor-only `closefd` contract CPython exposes through `io.open()`.
  `BytesIO.read1()` now exposes the one-buffer binary-read contract.
  Native in-memory streams now inherit their CPython `_BufferedIOBase` and
  `_TextIOBase` relationships, including IOBase helper methods.
  `StringIO` exposes CPython's `encoding`, `errors`, and `line_buffering`
  text-stream properties.
  In-memory `BytesIO` and `StringIO` now support `copy.copy()` and protocol-4
  pickle round-trips, preserving both their contents and current cursor.
  Their `__getstate__()` and `__setstate__()` state tuples are also available
  for source-backed serialization, including the configured `StringIO` newline
  mode.
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
  `setsockopt()` now routes integer, bytes-like, and explicit null-buffer
  forms to Winsock and rejects invalid values instead of silently ignoring them.
  Native sockets now start non-inheritable and support `get_inheritable()` /
  `set_inheritable()` through Windows handle flags.
  `getblocking()` now reflects native blocking-mode transitions.
  Accepted native sockets, including `_accept()` descriptors, are made
  non-inheritable before exposure to Python.
  Native socket instances now expose `dup()`, producing an independent
  descriptor that preserves socket metadata and remains usable after the
  source socket closes.
  The Windows handle inheritance primitives required by source-backed
  `socket.socket.get_inheritable()` and `set_inheritable()` are available for
  real socket handles.
  Validation update: all three CPython 3.14 Windows signal tests pass, including
  `SIGBREAK`, invalid-signal errors, handler reset, and subprocess
  `KeyboardInterrupt` exit behavior.
  `raise_signal()` now invokes registered handlers and rejects unsupported
  signal values with `ValueError`.
  Remaining: broader address-family/service resolution, deeper selectors edge
  behavior, signal delivery, full
  `_overlapped` IOCP behavior, and platform constants.

- [~] _weakref and _collections
  Coverage: `tests/fixtures/core/weakref_module.py` covers reference and proxy
  lookup, live-reference equality, weak-reference enumeration, collection-time reference expiration,
  CPython weak-reference hash caching and dead-reference hash errors,
  callback-free reference reuse for the same live referent,
  the read-only `ref.__callback__` property for callback inspection,
  delegated proxy attribute mutation plus `len`, iteration, indexing,
  membership, truth-value, equality, string-conversion, and item-mutation forwarding, and CPython's unhashable
  proxy contract. Proxies also forward representation, byte conversion, integer
  conversion, addition, subtraction, multiplication, floor division, matrix
  multiplication (including reflected and in-place forms), index conversion,
  negation, and inversion. Augmented floor division and matrix multiplication
  dispatch their CPython in-place hooks before normal or reflected fallbacks.
  Callback delivery through
  `gc.collect()` for both references
  and proxies, including last-created-first callback order, callback clearing
  after dispatch, and callback-bearing instance-cycle collection;
  `tests/fixtures/core/collections_queue_modules.py` covers the native collection
  dependency surface used by source-backed `collections`, including deque
  rotation, reversal, positional lookup with bounded index searches, insertion, bounded representation, and
  independent `copy()` results. CPython 3.14 focused `TestBasic` coverage also
  passes deque comparisons, concatenation, in-place concatenation, and in-place
  repetition. Native deque reducers support source-backed `copy.copy()` and
  protocol-4 pickle round-trips and use CPython's constructor/state/item-
  iterator reduce tuple; `deque.__reduce_ex__()` accepts index-like protocol
  arguments; `defaultdict.__reduce_ex__()` likewise accepts index-like protocol
  arguments; native defaultdict reducers preserve both
  mapping entries and default factories through copy and protocol-4 pickle,
  and `|` / `|=` preserve defaultdict type and factory. Reflected mapping
  unions (`dict | defaultdict`) likewise preserve the right-hand defaultdict
  type and default factory. Missing keys on a factory-less defaultdict retain
  the original key in `KeyError.args`.
  Deque search operations honor user-defined equality and reject mutation during
  comparison, matching the CPython container safety contract.
  Native deque integer-taking APIs accept the full `__index__` protocol for
  construction, positional access and mutation, search bounds, rotation,
  insertion, and repetition.
  `deque.extend()` and `extendleft()` report CPython's `TypeError` for
  non-iterable inputs while preserving exceptions raised by an input iterator.
  Native deque exposes an allocation-aware `__sizeof__()` result for container
  introspection.
  Forward and reverse deque iterators retain their source container and reject
  mutation with CPython's `RuntimeError` instead of iterating a stale snapshot.
  Validation: focused CPython 3.14 `test_weakref.ReferencesTestCase` passes all
  51 tests with its 4 expected skips, including cyclic callback invalidation;
  focused CPython 3.14 `test_deque.TestBasic` cases pass for copy, pickle,
  comparisons, concatenation, and in-place operations.
  Remaining: full weakref callback lifecycle timing and proxy parity; deque operation,
  iterator, comparison, copy/pickle, and representation parity; plus
  defaultdict/OrderedDict parity. Focused CPython 3.14 	est_deque.TestBasic\n  mutation-search and recursive-representation checks now pass; full-class validation\n  remains pending because the stress cases require a dedicated long-running run.

- [~] zlib and zipimport
  Coverage: `tests/fixtures/core/zlib_module.py`, `tests/fixtures/core/zipfile_module.py`, `tests/fixtures/core/zipimport_module.py`, `tests/fixtures/core/sys_path_importer_cache.py`.
  Validation: CPython 3.14 `test_zipimport` focused checks pass for bad archives,
  source/bytecode selection, nested package prefixes, direct member data, and
  cache invalidation. The core importer fixture also exercises `find_spec` and
  `get_filename` directly, including deflated ZIP members using data descriptors.
  Native zlib streaming tracks `eof`, `unconsumed_tail`,
  and accumulated `unused_data` after a stream ends; compressor and decompressor
  objects can be copied while their streams are active, including configured
  preset dictionaries; gzip-wrapped streaming is covered through `wbits`.
  Terminal compressor state now reports `zlib.error` for repeat flushes,
  invalid flush modes, and writes after `Z_FINISH`, matching CPython
  stream-finalization behavior.
  Bounded decompression now retains pending compressed input safely so `flush()`
  drains the remaining decoded stream, including on a copied decompressor.
  Completed decompressors can also be copied, retaining their terminal `eof`
  and accumulated `unused_data` state.
  One-shot `decompress()` validates `wbits` and `bufsize` argument types and
  rejects negative buffer sizes with CPython-compatible errors.
  Invalid compressed window sizes retain zlib's CPython-compatible diagnostic
  text instead of exposing only the native numeric error code.
  The native module exports CPython's standard compression strategy and flush
  constants, including `Z_RLE`, `Z_FIXED`, `Z_BLOCK`, and `Z_TREES`.
  One-shot `compress()` accepts its `level` keyword, while `decompress()`
  accepts `wbits` and `bufsize` keywords; `DEF_BUF_SIZE` is exported.
  One-shot compression also honors `wbits` for raw-deflate and gzip wrapper
  streams.
  Streaming decompressor `decompress()` accepts its `max_length` keyword and
  stream methods reject unsupported keyword calls with `TypeError`.
  Negative streaming `max_length` values raise CPython-compatible `ValueError`.
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
  Native `Pickler.fast` defaults to zero and forwards configured values to its
  source-backed delegate.
  Default native persistent-ID hooks mirror CPython: `Pickler.persistent_id()`
  returns `None`, while `Unpickler.persistent_load()` raises `UnpicklingError`.
  Default native `Unpickler.find_class()` delegates CPython-compatible module
  and global lookup to the source-backed unpickler.
  Validation: 62 focused CPython 3.14 `test_marshal` cases pass for scalars,
  containers, errors, byte buffers, code objects, compatibility, interning, and
  slices (with two platform skips).
  `marshal.dumps()` rejects non-integer version arguments with `TypeError`.
  `marshal.loads()` preserves the buffer API's released-memoryview `ValueError`.
  Remaining: full pickle protocol compatibility, extension codes, and
  remaining marshal stress/C-API cases.
