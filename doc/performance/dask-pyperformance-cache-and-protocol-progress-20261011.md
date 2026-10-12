# Dask pyperformance compatibility trace — 2026-10-11

The full pyperformance baseline still measures XLang3 at **0.128× CPython
3.14.7** on 88 matched cases. This trace records newer Dask failure progress;
it does not claim a Dask timing or an aggregate speedup.

## Stale compiler cache

The Dask import initially failed constructing `distributed.scheduler.ClientState`:
its final `__annotations__` contained `client_key`, `_hash`, and the other
class fields, but `__slots__` instead contained module annotation keys such as
`TaskStateState` and `ALL_TASK_STATES`. A fresh standalone reproduction of the
class body matched CPython, while importing the installed package did not.
Inspection showed XLang was loading the source-adjacent
`scheduler.xlang3-314.pyc`, whose header still used private magic `OX\x13\n`.
The new live class-annotation lowering was structurally compatible with that
cache, so the loader accepted stale IR based on the unchanged source mtime and
size.

The private magic is now `OX\x14\n`. After rebuilding, XLang recompiled the
installed source and `ClientState.__slots__` exactly matched
`ClientState.__annotations__`. This is a compiler-cache invalidation fix, not a
Python 3.14 cache format change. See [the class parity fixture](../../tests/fixtures/core/class_future_annotations_live_mapping.py).

## Dask failure progression

With current source recompiled, successive Dask attempts exposed missing
`psutil._psutil_windows` native APIs required by the unchanged psutil package:
`cpu_count_logical` and `proc_memory_info`. These were added to XLang3's native
Windows module with CPython's argument and return shapes. The existing
FastAPI/psutil contract now checks CPU count parity, the ten-field process
memory tuple, and public `Process.memory_info()` values.

The next failure was `functools.partial(HeapSet[TaskState], ...)` raising
`TypeError: the first argument must be callable`. CPython reports a
`types.GenericAlias` as callable when its origin is callable; XLang's call
dispatcher already forwarded calls to alias origins, but `callable()` did not
reflect that. `value_is_callable()` now follows the origin and leaves union
aliases non-callable. [The fixture](../../tests/fixtures/core/generic_alias_callable.py)
covers both cases and construction through `functools.partial`.

The subsequent Dask run advanced into graph transfer but failed while
deserializing a `ToPickle(HighLevelGraph)` payload with
`TypeError: object.__init__ expected no arguments`; communication then failed
and pyperf timed out the case. The exact first 5,101-byte graph payload from
the XLang3 log loaded successfully with CPython 3.14.7, narrowing the next
investigation to XLang3's pickle reconstruction path rather than malformed
Dask input. Instrumenting the pure-Python unpickler's `REDUCE` opcode locates
the failure at `types.CodeType(...)`: cloudpickle is rebuilding the serialized
`inc` function code, while XLang3 currently exposes a synthetic CPython-style
`co_code` view but has no matching `CodeType` constructor that can restore an
executable XLang code object. The prior `bytes.join` and canceled-futures
messages are downstream communication failures after that graph restore fails.

## Validation and artifacts

- Full Python fixture suite passed after the class annotation and generic-alias
  changes: `data/generic-alias-callable-20261011-fixtures.log`.
- The focused native psutil contract passed against the fixed Release
  executable and CPython 3.14.7 dependency installation.
- The Dask runs are preserved in `data/pyperformance-xlang3-dask-pyc-magic-20261011-r1.log` through `-r4.log`; only `-r4` progressed beyond import and `partial` construction, and it timed out without a measurement.
- Latest Release runtime SHA-256: `E4C8ACE7413DC3782AC62513733EE664D3394BD942E666A126E9201711758249`.
- The executable path remains `build-repro/main-verify-20261006/Release/xlang3.exe`.
