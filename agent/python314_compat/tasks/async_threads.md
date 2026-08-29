<!--
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->
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

