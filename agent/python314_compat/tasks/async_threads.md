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
  Remaining: resumable frame completeness, cancellation, exception propagation,
  async generator lifecycle, and protocol tests driven by real CPython
  `Lib/asyncio`.

- [~] CPython `asyncio` package over runtime async primitives
  Coverage: `tests/fixtures/probes/system_stdlib/asyncio_probe.py` verifies
  real CPython 3.14 `Lib/asyncio` import over XLang3 plus `_overlapped`
  foundation. Runtime fixes now preserve caller globals across nested
  interpreter/import execution, so `inspect.py` can generate `CO_*` constants
  through live `globals()`.
  Remaining: complete truthful Windows proactor/IOCP behavior enough for
  `asyncio.run()` and event-loop shutdown; do not restore a public native
  `asyncio` module.
