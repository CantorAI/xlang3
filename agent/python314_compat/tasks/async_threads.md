# Async, Task, And Thread Tasks

- [x] basic threading module
  Coverage: `tests/fixtures/core/threading_module.py`
  Remaining: none for the current basic surface.

- [~] native thread execution model
  Coverage: `tests/fixtures/core/threading_module.py`, `tests/fixtures/core/trace_hooks.py`
  Remaining: CPython-compatible thread lifecycle, lock/condition semantics, trace/profile inheritance edge cases, and shutdown behavior.

- [~] coroutine and await model
  Coverage: `tests/fixtures/core/async_syntax.py`, `tests/fixtures/core/task_async.py`
  Remaining: resumable frame completeness, cancellation, exception propagation, and async generator lifecycle.

- [~] asyncio facade
  Coverage: `tests/fixtures/core/asyncio_module.py`
  Remaining: real selector/scheduler integration, task cancellation, futures, transports, and event loop policy.

