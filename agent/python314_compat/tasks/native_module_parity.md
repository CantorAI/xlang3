# Native Module Parity Tasks

Goal: complete the CPython 3.14 native-module surface needed by the standard
library. Keep using XLang's existing native-module architecture. This task does
not split modules out of `xlang3_runtime.dll`, reorganize existing packaging,
or pursue runtime-size reduction.

## Non-negotiable implementation boundary

- Match CPython's C/native versus Python-source boundary. A module implemented
  natively by CPython may be implemented in C++ by XLang. CPython `Lib/*.py`
  packages and wrappers must remain Python source.
- Do not translate `asyncio`, `ssl`, `lzma`, `decimal`, `xml`, `uuid`,
  `zoneinfo`, or other pure Python library code into C++.
- Do not load or host CPython native binaries and do not depend on `python.exe`.
  Implement the required native behavior using XLang's runtime and ABI under
  `xlang3.exe`.
- Do not register placeholder modules. Once an optional accelerator import
  succeeds, CPython's Python wrapper may replace its working fallback with the
  native exports. A partial module must not make existing behavior regress.
- Keep general runtime behavior natural. Do not special-case tests, module
  filenames, debugpy, or Visual Studio.
- Build and run focused tests after each change and the complete Release CTest
  suite before marking this goal complete. Do not commit until the user asks.

## Verified baseline (2026-09-13)

- XLang registers its built-in native standard-library modules from
  `src/builtins/builtins.cpp` using implementations under
  `src/runtime/modules/system`.
- `_sqlite3` already resolves through XLang's native package loader, although
  its API still requires a full parity audit.
- XLang has native coroutine, `await`, async-generator, socket, overlapped I/O,
  subprocess, and XLang-specific thread-backed `task.Task` machinery. It does
  not register a CPython-compatible `_asyncio` module. CPython's Python
  `asyncio` package therefore retains its Python `Future`, `Task`, loop, and
  task-tracking implementations.
- A direct import audit found several names in XLang's reported standard/core
  module inventories that cannot actually be imported. Module inventories
  must reflect real registered or loadable modules.

- [ ] Audit `_sqlite3` native compatibility.
  Verify ownership, cleanup, exceptions, connection/cursor APIs, and
  source-backed `sqlite3` behavior. Direct import currently succeeds, but the
  observed connection surface does not yet prove full CPython parity.

## Missing optional accelerators with working Python paths

These public Python libraries currently import under XLang, but their CPython
native accelerator is absent. Passing fallback tests is not accelerator
completion.

- [ ] `_asyncio` for `asyncio`
- [ ] `_decimal` for `decimal` (`_pydecimal.py` currently loads)
- [ ] `_elementtree` for `xml.etree.ElementTree`
- [ ] `_uuid` for `uuid`
- [ ] `_zoneinfo` for `zoneinfo`
- [x] `_bisect` for `bisect`
  Implemented as an XLang native module while retaining `Lib/bisect.py`.
  Coverage: all 46 CPython 3.14 `test.test_bisect` cases pass for both the
  native accelerator and forced Python fallback, including keyword arguments,
  key functions, non-bool comparisons, list subclasses, and error propagation;
  `xlang3_cli_compat` also passes.
- [ ] `_heapq` for `heapq`
- [ ] `_functools` for `functools`
- [ ] `_datetime` for `datetime`
- [ ] `_hmac` for `hmac`
- [ ] `_statistics` for `statistics`
- [ ] `_hashlib` and the missing `_md5`, `_sha1`, `_sha3`, and `_blake2`
  algorithms for `hashlib`; `_sha2` is currently available, while the public
  module logs failures for several other algorithms.

For every optional accelerator, retain and test the pure Python fallback as
well as the accelerated path.

## Missing native modules that break public imports

- [ ] `_csv` (`import csv` currently fails)
- [ ] `_symtable` (`import symtable` currently fails)
- [ ] `_tracemalloc` (`import tracemalloc` currently fails)
- [ ] `_lsprof` (`import cProfile` currently fails; `profile` works)
- [x] `cmath`
  Implemented as an XLang native module in the existing runtime packaging.
  Coverage: all 33 CPython 3.14 `test.test_cmath` cases pass, with the one
  upstream CPython implementation-detail errno test skipped by its own guard.
  The implementation includes CPython-compatible numeric conversion,
  non-finite and signed-zero behavior, domain/range exceptions, and `isclose`.
- [ ] `mmap`
- [ ] `_lzma` (`import lzma` currently fails)
- [~] `_ssl`
  Python 3.14's source `ssl.py` now imports over the separate
  `xlang__ssl.x3pkg` module, which reuses the bundled OpenSSL library. Current
  coverage includes constants and exceptions, randomness, OID lookup,
  `MemoryBIO`, Windows certificate/CRL enumeration, context trust and
  certificate loading, ALPN configuration, and a real TLS 1.3 client/server
  handshake plus encrypted data transfer over paired memory BIOs. Remaining
  parity includes socket wrapping, peer-certificate APIs, sessions, context
  statistics/cipher inspection, callbacks, and the rest of CPython's `_ssl`
  error and edge-case surface.
- [ ] `_tkinter`
- [ ] `_zstd`
- [ ] `winsound`

Also audit `_remote_debugging`, `_wmi`, `_interpchannels`, `_suggestions`,
`_types`, and `xxsubtype`. Classify each as a
required runtime module, optional platform/development module, test-only
module, or intentionally unsupported module. Do not advertise unavailable
modules as implemented.

- [x] `_interpreters` and `_interpqueues`: runtime-owned VM isolation and
  cross-runtime transport primitives. The real Python 3.14
  `concurrent.interpreters` wrapper passes create/prepare/exec/call/close and
  bounded queue coverage without CPython ABI/runtime dependencies.

## `_asyncio` design requirement

Do not alias XLang's existing `task.Task` to `_asyncio.Task`. XLang `task.Task`
owns a worker thread; CPython `_asyncio.Task` advances a coroutine on an event
loop, normally on the loop's thread.

A correct `_asyncio` package must expose the CPython 3.14 surface:

- `Future` and `Task`
- `_get_running_loop`, `_set_running_loop`, `get_running_loop`, and
  `get_event_loop`
- `_register_task`, `_unregister_task`, `_register_eager_task`, and
  `_unregister_eager_task`
- `_enter_task`, `_leave_task`, `_swap_current_task`, `current_task`, and
  `all_tasks`
- `future_add_to_awaited_by` and `future_discard_from_awaited_by`

Implement event-loop `Future` and `Task` state over XLang's existing native
coroutine/frame/await machinery. Cover pending, cancelled, and finished state;
results and exceptions; done callbacks; loop affinity; coroutine stepping and
wake-up; cancellation counters/messages; task names; context propagation;
current/all-task tracking; eager tasks; and awaited-by relationships. Register
`_asyncio` only when importing it cannot displace the working Python fallback
with an incomplete implementation.

## Validation requirements

- Add direct import tests for every native module and verify `__name__`,
  `__spec__`, loader identity, and expected XLang-native origin.
- Compare the public native surface and representative behavior with the same
  installed CPython 3.14 build. CPython is an external reference only.
- For accelerators, verify the Python wrapper selects the native class/function
  and separately verify its fallback when the accelerator is unavailable.
- Preserve the existing `asyncio_runtime_edges.py` behavior: gather/yield,
  cancellation and cleanup, coroutine inspection, TCP sockets, subprocesses,
  and pipe communication.
- Add `_asyncio` tests for Future/Task lifecycle, loop ownership, callbacks,
  cancellation, exceptions, context variables, current/all-task tracking,
  eager start, awaited-by tracking, and thread isolation.
- Verify from a clean XLang environment. Imports must not succeed accidentally
  from CPython native binaries.
- Generate module inventory data from modules that are actually registered or
  loadable. Test that advertised native names import, except names explicitly
  classified as unavailable metadata entries required by CPython semantics.
- Run the full Release CTest suite and require all tests to pass before marking
  this task complete.

## Completion criteria

This task is complete when:

1. Native modules follow the same implementation boundary as CPython: native
   CPython modules may be C++; pure Python libraries remain Python.
2. Existing module placement remains unchanged unless implementation
   correctness requires a focused change. Module separation and runtime-size
   reduction are outside this task.
3. Required public standard-library imports work without CPython binaries.
4. Optional accelerators either have compatible native implementations or are
   explicitly marked as remaining; fallback success alone is not called native
   completion.
5. `_asyncio` imports and accelerates the source-backed `asyncio` package
   without using XLang's thread-backed `task.Task` as a false substitute.
6. Module inventories match real availability, the full Release suite passes,
   and temporary files/logs are removed.

## New-chat continuation prompt

Continue `D:\CantorAI\xlang3\agent\python314_compat\tasks\native_module_parity.md`
autonomously until its completion criteria are satisfied. Start by establishing
a clean CPython 3.14 native-module API and behavior baseline. Keep the current
XLang module packaging and implement missing native behavior through XLang's
existing runtime architecture. Validate modules in dependency order,
prioritizing broken public imports and `_asyncio`. Build and test every cycle.
Never implement pure Python CPython library code in C++, never use CPython as a
runtime host, never register placeholders, and do not commit until the user
asks.
