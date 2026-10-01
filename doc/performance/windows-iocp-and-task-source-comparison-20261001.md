# Windows I/O completion and asyncio scheduling comparison

The native `_ssl` clean-EOF fix lets the unchanged official TCP/SSL workload
finish in single-value mode at 13.0 seconds. A full fast attempt still timed
out after 300 seconds, having advanced through several workers. This exposed
two separate paths to investigate: native Windows I/O completion delivery and
Python Task/Future scheduling.

## Native completion delivery

CPython 3.14.7's
[`Modules/overlapped.c`](https://github.com/python/cpython/blob/v3.14.7/Modules/overlapped.c#L203-L265)
creates an OS completion port and waits with Windows `GetQueuedCompletionStatus`.
XLang3's own native `_overlapped` implementation currently creates a registry
port, scans outstanding operations with nonblocking overlapped-result queries,
and waits on a condition variable with a 1 ms polling interval when I/O is
pending. Socket completion itself does not signal that condition variable;
synthetically posted completions do. This is a concrete native implementation
difference, not a pure-Python library to move into C++.

The [same-source diagnostic](../../benchmarks/diagnostics/iocp_completion_latency.py)
posts an overlapped one-byte receive on a loopback socket and wakes a producer
thread to send the byte, then waits for and checks its completion. It performs
200 sequential requests, checking error code, transferred size, key, address,
and returned bytes. It avoids asyncio Task/Future, but includes Python thread
wakeup and generic runtime costs; it does not isolate the polling timer alone.

| Completion diagnostic | CPython 3.14.7 | XLang3 control |
|---|---:|---:|
| Median | 82.2 us | 4,575.8 us |
| Mean | 200.1 us | 7,422.4 us |
| Minimum | 65.1 us | 1,207.5 us |
| Maximum | 20,860.1 us | 23,988.0 us |

The observed median is 55.7x higher in this diagnostic. The samples contain
outliers and thread scheduling effects, so this is not an official TCP score
or a causal estimate of the whole TCP slowdown. It does justify measuring an
OS-driven completion path rather than repeatedly tuning generator-send calls.

Raw outputs: [CPython](data/iocp-completion-latency-cpython314-20261001.txt),
[XLang3](data/iocp-completion-latency-xlang3-control-20261001.txt).

An implementation trial must preserve completion keys/addresses, immediate
and delayed operations, synthetic posts and wakeups, cancellation, registered
waits, timeout/infinite-wait semantics, and native buffer lifetime through
completion. It must retain required cross-platform behavior and pass the
existing `overlapped_iocp_wait` and asyncio fixtures, full CTest, the complete
fixed regression gate, and official TCP/SSL comparisons. No completion-path
code change or speedup is claimed in this report.

## Task/Future call-event profile

The [scaled-tree profile](../../benchmarks/diagnostics/async_tree_call_profile.py)
ran the same 3-level, 3-branch tree for 5 iterations under both runtimes.
CPython reports `_asyncio.Task` and `_asyncio.Future`; XLang3 reports
`asyncio.tasks.Task` and `asyncio.futures.Future`.

The instrumented trace records 5,742 Python asyncio call events for CPython
and 12,177 for XLang3. Shared event-loop paths have the same counts: 515
`call_soon`, 515 `Handle.__init__`, and 510 `Handle._run` calls. XLang3's visible
Python work includes 825 `Future.done`, 410 `Future.cancelled`, and 405
`Future.exception` calls, plus Python Task registration and stepping.
CPython's native implementation does not emit equivalent Python call events
inside native methods; these totals are not comparable instruction counts,
execution times, or a predicted speedup.

Raw outputs: [CPython](data/async-tree-call-events-cpython314-20261001.txt),
[XLang3](data/async-tree-call-events-xlang3-20261001.txt).
This supports the previously identified native-compatible `_asyncio`
Task/Future boundary. The Python event loop remains Python. The semantic and
GC requirements are recorded in the
[scheduling investigation](asyncio-task-scheduling-trial-20261001.md).
