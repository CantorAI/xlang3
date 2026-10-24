# Native `_asyncio` candidate: implementation and validation

**The candidate is built and correctness-validated.** Its expanded asyncio
fixture passes on XLang3 and CPython 3.14, Release CTest passes 55/55, and the
fixed-baseline performance gate passes. The targeted Task-state follow-up
measured a repeatable 1.28× improvement on `async_tree_none`, but the current
candidate is still 23.8× slower than CPython on that case. See the detailed
[Task-state comparison](asyncio-task-thread-state-20261001.md).

The saved initial full pyperformance pair remains the all-case record for its
then-current Release binaries. It is not a post-Task-state full-suite result;
the full 97-case comparison still needs to be rerun against the current build.

## Why this work follows the IOCP investigation

The [native IOCP investigation](windows-iocp-native-queue-20261001.md) removed
the roughly 15 ms polling floor in the local completion-latency diagnostic.
Official TCP/TLS throughput differences against the polling control were
insignificant. The measured XLang3 TCP transfer also spent approximately 97%
of its wall time on CPU. Improving a waiting mechanism therefore did not
address the bulk-transfer CPU bottleneck.

CPython 3.14 uses native `_asyncio.Future` and `_asyncio.Task`; XLang3's
previous build uses the standard library's Python fallback. This adds Python
method calls, Python-level state transitions, callback tuple/list
allocation, and a suspended Python generator frame for a Future await. The
candidate targets that native-module difference. It is a hypothesis about the
measured bottleneck, not proof that `_asyncio` explains all remaining cost.

The checked-in pyperformance async-tree case recursively creates 6 children
at each of 6 levels. The ordinary `async_tree_none` case measured **254.011 ms
on CPython 3.14.7** in the saved full reference run. Its gather tree schedules
55,986 child Tasks and creates 9,331 intermediate gather Futures per
invocation; each Task must enter its coroutine and either yield or complete.
The interrupted XLang3 run exceeded the equal 120-second per-case cap on this
case, so it yielded no valid timing. The restarted full run is measuring the
same fixed binary. This makes Task entry a direct target; only candidate
timings can show whether the native change closes the gap.

A process snapshots during that restarted attempt show a second symptom:

| XLang3 case | Worker observation | Result |
|---|---:|---|
| `async_tree_none` | 2.86 GB working set near its 120-second deadline | Timed out |
| `async_tree_cpu_io_mixed` | 2.55 GB working set after about 30 seconds | Timed out |
| `async_tree_cpu_io_mixed_tg` | 3.61 GB after about 34 seconds; 4.65 GB after about 99 seconds | Still running at observation |

These are live process snapshots, not peak-memory measurements or completed
benchmark timings. They make per-task allocation, callback retention and
cycle-collector cost high-priority parts of the review alongside dispatch
overhead. The complete attempted case list and final statuses remain in the
full-run log and generated table after all 97 cases finish.

The restarted all-97 run has completed. All 16 official `async_tree` variants
hit the 120-second cap; the shared-dependency CPython reference completed
those same cases in 0.088–0.600 seconds. XLang3's `asyncio_tcp`,
`asyncio_tcp_ssl` and `asyncio_websockets` cases completed in 5.54, 12.0 and
0.519 seconds, versus 0.77693, 4.37993 and 0.19249 seconds for CPython:
**7.13×, 2.74× and 2.70× slower**. Pyperf warned that each XLang3 result had
too few samples for a stable result, so treat the ratios as provisional. The
[full 97-definition comparison and chart](pyperformance-xlang3-vs-cpython314-native-iocp-shared-deps-full-fast-20261001.md)
records every completion, partial result, timeout and failure.

The reference is [CPython 3.14.7 `_asynciomodule.c`](https://github.com/python/cpython/blob/v3.14.7/Modules/_asynciomodule.c),
particularly the Future fields and iterator, callback scheduling, task step,
wakeup, eager start, cancellation and task introspection implementations.
XLang3 owns this module; it does not import CPython's native extension.

## Performance invariants in the candidate

- Future state, result, exception, blocking flag and cancellation state live
  in native fields. A completed Future await reads those fields directly,
  including for a Future subclass, as CPython's native iterator does.
- A Future await uses a compact native iterator, with no Python generator
  frame. The iterator retains its Future after successful `StopIteration`;
  `close()` and a valid `throw()` release it. Invalid `throw()` must preserve
  ownership. Frame removal must not change this lifetime contract.
- The first callback and its context occupy inline native fields. Further
  callbacks allocate a vector and preserve registration order. Completion
  queues callbacks through the unchanged Python event loop, never inline.
  During the first `call_soon`, remaining callbacks stay visible to reentrant
  removal. They are detached for iteration only after that scheduling call
  returns. A scheduling failure clears the remaining callbacks; exception
  logging is enabled only after successful scheduling.
- An omitted callback context is captured at registration. Explicit
  `context=None` remains None so the event loop captures context when the
  callback is scheduled. Treating these as identical changes Python behavior.
- A single awaited-by reference is inline; a second registration creates a
  set. Discard removes ownership before resuming the waiting Task.
- Task step/wakeup callbacks bind directly to the native functions, matching
  CPython's internal wrappers. Exact native Futures have direct state and
  callback access. Subclasses and foreign futures retain dynamic `get_loop`,
  blocking descriptor, `add_done_callback`, `result` and `cancel` dispatch
  where CPython uses it.
- Native XLang coroutines resume through `generator_send`/`generator_throw`
  directly inside Task step. This mirrors CPython 3.14's `PyIter_Send` path:
  it avoids repeated `.send` lookup and preserves the coroutine return value
  without manufacturing a StopIteration exception for every completion.
- Task names retain a numeric counter until `get_name` or repr needs a string.
  Completed eager Tasks release their coroutine; suspended Tasks retain it.
- Task registration uses a weak intrusive list, like CPython's Task list. It
  avoids one set-node allocation per live Task during the 55,986-task tree
  workload. The owning registry can outlive its module through a Task's
  shared pointer. Destruction unregisters before releasing payload references.
  A registry lock must never span a Python call or destruction of an owning
  snapshot.
- TaskFields now live inline in the native Future payload's optional storage.
  A Task therefore avoids the extra `TaskFields` heap allocation and keeps the
  address stable through reentrant `__init__`; ordinary Future objects leave
  the optional disengaged. This follows CPython's embedded Task layout and
  removes one native allocation from every Task construction, at the cost of
  the inline storage's footprint in each Future payload.
- Native reference changes publish cycle-collector edges before reentering
  Python. Reference counting alone would leak Future/result, Task/waiter and
  callback cycles. The common edge set (up to 24 Values) is assembled in a
  stack array, while callback-heavy Futures use a dynamic buffer. The object
  model reuses its stored `Object*` vector capacity, avoiding a fresh vector
  allocation on each Task step. The instance remains temporarily unregistered
  while its edge mirror is replaced. Validate these collector changes against
  native cycle/clear tests before measuring their gain.
- Task payload addresses remain stable across reentrant initialization. Python
  descriptor, coroutine and loop hooks can call back into `__init__` while a
  native operation holds a field reference.

The event loop, transports, streams, synchronization helpers, TaskGroup,
timeouts and other pure-Python asyncio library code remain Python. Repr and
stack helpers still call the original `asyncio.base_futures` and
`asyncio.base_tasks` Python implementations. The running-loop helper initially
uses the event module's existing `threading.local` and PID guard; it does not
introduce an owning C++ TLS Value with a separate interpreter lifetime.

## Candidate files and tests

- `src/runtime/modules/system/asyncio_native.h`: native payloads and helpers.
- `asyncio_future.cpp`: Future methods, properties, callbacks and iterator.
- `asyncio_task.cpp`: Task stepping, wakeups, context, eager start, cancellation,
  finalization and inspection.
- `asyncio_module.cpp`: module registration, running-loop and task registry
  helpers, native/third-party task tracking and awaited-by support.
- `src/runtime/object_model.cpp`: retain per-instance native GC edge-vector
  capacity across updates, avoiding repeated heap buffers for Task transitions
  while preserving the existing temporarily-unregistered replacement window.
- `tests/native/asyncio_accelerator.py`: contracts for iterator retention,
  invalid throw ownership, read-only fields, context ordering, callback
  reentrancy, exception identity, cancellation and uncancel, eager restoration,
  native subclass awaiting and wakeup overrides, foreign futures, invalid
  yields, self-await, loop mismatches, weak registries and collector edges.

The expanded expected output was checked on CPython 3.14 and XLang3. The
current Release executable and runtime DLL identities are recorded in the
[Task-state comparison](asyncio-task-thread-state-20261001.md).

## Remaining evidence before acceptance

1. Run the full unchanged 97-case pyperformance suite against the current
   XLang3 Release build and CPython 3.14 with shared dependencies, preserving
   all completion, partial, timeout, and failure records.
2. Measure official async-tree, TCP, TLS, and WebSocket cases with stable
   samples and correctly recorded binary identities. Keep capped/failed cases
   visible; a workload replica or one-value result cannot establish a suite
   speedup.
3. Continue profiling the remaining VM calls and allocations. Preserve
   subclass dispatch, tracing semantics, and Python implementations for pure
   Python standard-library modules.
4. Compare the full current suite against the August performance binary and
   CPython 3.14 before claiming the broad speed goal is met. The targeted
   asyncio gain and fixed local regression pass do not establish an overall
   win.
5. Commit/push only after the complete intended validation is recorded and
   the user-requested deliverables are ready.
