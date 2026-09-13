# Async, Task, And Thread Tasks

- [x] CPython `threading.py` over `_thread`
  Coverage: `tests/fixtures/core/threading_runtime_edges.py` imports CPython
  3.14 `Lib/threading.py` through the normal source loader and exercises it on
  the native `_thread` dependency. No public native `threading` facade exists.

- [x] native thread execution model
  Coverage: `tests/fixtures/core/threading_runtime_edges.py` covers public
  thread startup, identifiers, repeated-start and invalid-join errors,
  non-daemon shutdown waiting, daemon mutation, per-thread local dictionaries,
  Lock/RLock/Event/Semaphore/Condition/Barrier behavior, and trace/profile
  inheritance with CPython 3.14 differential output.

- [x] coroutine and await model
  Coverage: `tests/fixtures/core/asyncio_runtime_edges.py`,
  `tests/fixtures/core/async_syntax.py`, `tests/fixtures/core/task_async.py`,
  `tests/fixtures/core/sys_coroutine_origin_metadata.py`, and
  `tests/fixtures/compat_sections/function_and_class_syntax.py` cover lazy
  coroutine creation, generic `__await__` iterators, scheduler yields,
  `asyncio.gather`, cancellation messages and nested-finally propagation,
  waiter cleanup, async iteration/context managers/generators, coroutine
  origin metadata, and the CPython inspection states and attributes
  `cr_running`, `cr_suspended`, `cr_frame`, `cr_code`, `cr_await`, `cr_origin`,
  `__name__`, and `__qualname__`.

- [x] CPython `asyncio` package over runtime async primitives
  Coverage: `tests/fixtures/core/asyncio_runtime_edges.py` imports CPython
  3.14 `Lib/asyncio` through the normal source loader and runs its Windows
  `ProactorEventLoop` over native `_overlapped` primitives. The fixture covers
  loopback TCP accept/connect/read/write/drain/close, process creation and
  completion waits, stdout/stderr overlapped reads, stdin overlapped writes,
  process exit status, task scheduling, cancellation, and event-loop shutdown.
  `tests/fixtures/probes/system_stdlib/asyncio_probe.py` retains the lower-level
  import and startup probe. No public native `asyncio` facade exists.
