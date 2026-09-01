# Async, Task, And Thread Tasks

- [~] CPython `threading.py` over `_thread`
  Coverage: pending in the default fixture runner; add a future fixture that
  imports CPython `Lib/threading.py` through the normal import path.
  Remaining: run the real CPython 3.14 `Lib/threading.py` on top of `_thread`;
  do not restore a public native `threading` module.

- [~] native thread execution model
  Coverage: pending in the default fixture runner; add fixtures around `_thread`
  plus CPython `threading.py` once the dependency surface is ready.
  Remaining: CPython-compatible thread lifecycle, lock/condition semantics, trace/profile inheritance edge cases, and shutdown behavior.

- [~] coroutine and await model
  Coverage: `tests/fixtures/core/async_syntax.py`, `tests/fixtures/core/task_async.py`
  plus `tests/fixtures/compat_sections/function_and_class_syntax.py` for
  `asyncio.run`, `async for`, `async with`, and async generator methods.
  Runtime now preserves current-frame state as a thread-local stack across
  nested VM execution, so coroutine `send()` inside `asyncio.Task.__step()` no
  longer destroys the caller frame needed by zero-argument `super()` and debug
  frame APIs.
  Remaining: cancellation edge cases, scheduler-yielding coroutine states, and
  exact CPython coroutine inspection APIs.

- [~] CPython `asyncio` package over runtime async primitives
  Coverage: `tests/fixtures/probes/system_stdlib/asyncio_probe.py` verifies
  real CPython 3.14 `Lib/asyncio` import over XLang3 plus `_overlapped`
  foundation, and now runs `asyncio.run()` through event-loop shutdown. Runtime
  fixes preserve caller globals/current frames across nested interpreter/import
  execution, prefer Python `__iter__` protocol for user objects before internal
  storage fallbacks, and give `_overlapped` a native IOCP completion registry
  for immediately completed/cancelled operations.
  Remaining: full Windows proactor socket/process I/O behavior; do not restore
  a public native `asyncio` module.
