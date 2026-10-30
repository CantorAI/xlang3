# Linux IPC recovery after client death (2026-10-30)

After a JSON-corrupted client aborted, Azure's surviving Cantor process stopped
answering local RPC probes. A private all-thread snapshot showed a worker in
glibc `pthread_cond_broadcast` / `__condvar_quiesce_and_switch_g1`, holding the
shared region mutex. Its normal shutdown subsequently hit the existing systemd
90-second stop timeout. This was separate from the JSON parser crash.

A standalone listener/client pair with no Cantor or JSON reproduced the hang:
kill a client after its request entered the server, then a fresh healthy call
times out. Increasing the number of shared slots would not release the blocked
condition-variable waiter groups or their region mutex.

Linux now uses a process-shared futex generation word for notifications. The
generation is captured before unlocking and checked by the kernel when waiting,
so a wake between unlock and wait cannot be lost. A killed waiter leaves no
glibc waiter bookkeeping to strand later broadcasts. The region mutex is robust;
each lock path, including probes and queue validity checks, makes an abandoned
owner's mutex consistent. Existing dead-owner slot cleanup, request identities,
late-reply rejection and cancellation remain in use.

Response timeouts now use a fixed monotonic deadline with millisecond waits,
rather than resetting elapsed seconds after a wake. The response reader clears
only an obsolete condition-wait diagnostic before reading an actual reply.
Slot count, worker count, resource policy and instruction paths are unchanged.

Linux shared-memory protocol is **5**; macOS and Windows retain protocol **4**.
The condition storage's size and alignment are preserved, and incompatible
local clients fail before accessing synchronization primitives. Both processes
on one Linux IPC endpoint must be upgraded together; network cluster peers
do not share this local memory layout and need no coordinated protocol update.
The existing proxy attribute wrapper can surface a rejected endpoint as
`AttributeError`, so mixed-version checks also require same-version success
controls and a prompt normal failure rather than a particular error message.

The isolated stress completed 200 killed-client/recovery rounds. New Linux CTest
coverage kills 64 owned callers and one owned process holding the region mutex,
then verifies healthy RPCs and graceful listener teardown. A newly registered
150 ms timeout fixture exercises 40 expired calls, slot reuse, late replies,
and cooperative cancellation. These changes passed 24 native/IPC/Cantor/Galaxy/
Principia checks and the 449-case fixture differential (432 passes, the same
17 baseline failures, zero new failures).

The first broader native test attempt could not find its unbuilt C++ IPC client;
building that target and rerunning passed all 24 checks. The initial compatibility
helper used an incomplete preserved CLI directory and assumed an endpoint error
would survive the proxy wrapper. Its corrected full-baseline controls passed in
both version directions. These harness failures are retained with the raw logs.

The pre-change accepted Release and the immediately preceding executable/runtime
were preserved. The complete eleven-case default gate passed on CPU 2, with
21 balanced pairs, five warmups and the original 10% limit, without concurrent
builds or tests. Its largest candidate/baseline ratio is 1.007x (GC traversal).
The unchanged VM function retains size `0x2f37c9`; all 90 C API exports remain.

- [Complete performance gate](data/linux-ipc-client-death-20261030-gate.json)
- [Fixture differential](data/linux-ipc-client-death-20261030-fixtures.json)
- [ABI and VM layout](data/linux-ipc-client-death-20261030-abi.json)

Production sustained testing is separate from these checks.

## Azure crash and IPC acceptance

The compiled `9c089b14` runtime (`c03fde87...`) was deployed beside the previous
Release and activated through the existing service path. All 2891 tracked files
were verified, and the original external Database, CantorStore and Logs links
were retained. A real registry reply with a one-shot throwing observer was
caught without process exit; that probe unsubscribed and stopped its own module.

The production run completed 2700.27 seconds and 10,482 signed calls with no
call errors, no core or API restart, and one unchanged cached client proxy.
Its worker threads joined normally. Core/API PIDs were 872751/873110 throughout.
The last five minutes measured median 4.85 ms / p95 10.98 ms, versus 5.33 ms /
11.68 ms in the first five minutes. The full run includes a 10.01-second maximum;
it is not a bounded-tail latency guarantee. Public Cluster polling was added
after 7m37s, so these windows do not have identical total UI traffic.

Real serial HTTP streaming passed at the beginning and end for both 2B and 27B
models, using actual completion usage rather than chunk counts. These few
samples establish availability, not native GPU decode performance. The owned
debugger was detached after the run without signalling or restarting either
application. Source, state links, runtime hash and HTTP 200 were checked again.

[Production receipt and full time series](data/azure-native-stability-20261030-production.json)
also preserve the first candidate's failures and the corrected staging omission.
The shared Windows runtime and its protected test service were not replaced.

**Open finding:** API tracked objects and RSS still grew. An isolated same-Release
test leaves 40,000 function/cell objects from 10,000 unreachable closure pairs
even after explicit `gc.collect()`; CPython collects them. This is being
investigated separately. No production GC was forced, and memory convergence
is not certified by this crash/IPC acceptance run.
