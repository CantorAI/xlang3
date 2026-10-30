# Unseeded closures and ASGI JSON encoder cycles, 2026-10-30

V5 has completed the offline qualification below and is deployed to the Azure
Linux node. Its subsequent complete live interval **fails the original memory
gate** despite zero call errors or restarts. Production memory convergence is
not certified. No local Windows Release executable or shared CantorServe test
service was replaced.

The v4 candidate is **rejected for deployment**: its four-thread ASGI/GC soak
aborts with `corrupted size vs. prev_size` after the 660-second checkpoint
(25,815 completed requests). Stable RSS and passing serial/performance checks
do not qualify concurrent GC safety. The 900-second final acceptance was not
reached; neither the candidate nor the API maintenance helper was deployed.
The failed runtime and evidence are preserved for debugger reproduction.

The accelerated diagnostic (16 threads, 50 ms between collections) reproduces
a SIGSEGV after its 30-second checkpoint. The native stack is
`generator_vm_visit_references -> collect_isolated_native_instance_component
-> weakref_collect_cycles -> gc_collect`. The specialized pass holds only its
registry mutex, which does not serialize another interpreter's coroutine frame
resumption/retirement. V5 adds execution-before-registry protection to that
whole pass, protects module/frame preparation, and gives referrer/object
introspection the same graph-read serialization. The plain pass retains its
existing separate retirement scope; opaque metadata is still released after
that scope rather than under an added outer collection lock.

An audit corrected the C++ validation target: `generic_gc_cases.h` belongs to
`xlang3_interpreter_tests`, not `xlang3_runtime_value_tests`. The earlier 24
selected native tests did not execute the newly added header checks. Those
coverage claims are withdrawn until this actual target runs. The updated test
now enters native callbacks without a VM guard and checks that native graph
visitors execute serialized. The preserved v4 control fails exactly its two
collection/referrer checks; v5 passes the interpreter target, including the
previously unexecuted metadata/opaque-owner checks. Full qualification must
include this target (25 selected CTests in total) and a fresh long soak.

V5 subsequently passes all 25 selected native tests, the full 450-case fixture
comparison (433 pass, the same 17 baseline failures, zero new regressions),
and the native asyncio accelerator contracts, including Future and suspended
Task/Future cycle collection. Its complete fixed Release gate also exits 0:
the largest ratio is about 1.014 for deepcopy, and function calls are near
1.00. The accelerated 16-thread/50 ms diagnostic now completes 120 seconds and
17,535 requests without error; it is not a substitute for the original
900-second four-thread qualification. The unchanged API helper's default timer
and both shutdown paths pass again under V5. Runtime SHA-256:
`b06aa789a7b3bba4307d1231edff343e0a4509189c23e23e123f0202b321bb7c`.

The original four-thread workload then completes 900.09 seconds: 35,113
requests, zero errors, all worker threads joined and 984,040 objects reclaimed
across collections. Final RSS is 94,356 KiB (95,984 KiB immediately before
thread shutdown/final collection), with a 3,912 KiB delta from the first sample
after 120 seconds. Tracked objects remain bounded; sample records and allocator
caches are included in these observations. First/last bounded 6,000-request
windows have median/p95 latencies of 1.462/3.514 ms and 1.206/2.544 ms. This is
an offline ASGI/collection soak, not HTTP inference TTFT or an infinite-duration
memory guarantee. Production memory/latency acceptance remains separate.

## Reproduction and scope

The deployed `c03fde87` runtime survives the recorded 45-minute IPC/crash soak,
but the API heap grows. An isolated factory creates 10,000 independent pairs
of mutually captured functions without weakrefs. An explicit `gc.collect()`
reclaims zero of their 40,000 function/cell objects. CPython reclaims them.

A separate, socket-free workload uses the production Starlette 1.9.0/AnyIO
Python sources and serves 10,000 JSONResponse requests. Even collecting after
every 1,000 requests leaves about 280,000 additional objects: per request, 22
cells, three functions, one bound method, one JSONEncoder and one dictionary.
This reproduces growth without Cantor, IPC, authentication or model inference.

The generic collector previously excluded Function and BoundMethod. Its
specialized function collector is seeded only by registered weakrefs and
handles attribute/default back-edges, so it cannot discover these ordinary
closures. The recursive stdlib encoder also captures builtin types and native
intrinsics. `_json._floatstr` used an owning native cleanup object even for a
plain boolean option, making every recursive encoder a conservative boundary.

## Candidate ownership rules

- Enumerate function closure/default/keyword-default/annotation/attribute/doc
  storage and both owning fields of bound methods in generic trial deletion.
  Preserve duplicate ownership counts and exclude borrowed references.
- Keep function code, captured globals/builtins and IR owners opaque. Incoming
  references from those owners remain external roots. Pin this metadata until
  after graph publication and retirement, then release outside the collector's
  execution-lock scope while its reentry guard remains active.
- Treat captured classes as opaque metadata leaves, using the same full-snapshot
  pins that protect instance class metadata. A class back-edge is not subtracted
  and therefore still roots the component.
- Admit only native intrinsics with no attributes and no cleanup callback as
  leaves. Stateful native extensions, finalizers and weakref boundaries remain
  conservative. Tests retain the original opaque-owner exclusion.
- Give boolean `_json._floatstr` options immutable scalar state with no cleanup
  callback. Non-boolean options retain the original owning state and deferred
  interpretation. The stdlib JSON algorithm remains Python.

The resulting serial ASGI diagnostic reclaims 28,000 objects per 1,000 requests.
After 10,000 requests its tracked-object delta is 20, accounted for by the
diagnostic's retained sample records; the final samples have stable function
and cell counts, and RSS is approximately 72 MiB. This is not a throughput or
production memory-convergence benchmark.

## Validation and retained experiments

The first function-only candidate passes 24 native checks, 450 fixtures
(433 pass, 17 baseline-identical failures, zero new regressions), and the full
fixed 11-case Release gate. It does not reclaim the ASGI encoder cycles and is
not a complete fix. Adding bound methods and safe metadata leaves still fails
that reproduction until the boolean native cleanup boundary is removed.

The complete candidate again passes 24 native checks and the 450-case fixture
comparison. Focused coverage includes repeated unseeded mutual closures, live
closures, another thread's live root, duplicate defaults/annotations, bound
methods, captured types/intrinsics, repeated JSONResponse-style encoding and
both boolean NaN policies. C++ checks cover embedding roots, opaque global
back-edges, deferred native metadata cleanup, rejected GC reentry and retained
stateful-native boundaries.

Its first full performance run exits 2: ten cases pass and both function-call
attempts are inconclusive, with nominal ratios near 1.08 and confidence bounds
crossing the unchanged 1.10 boundary. A subsequent idle preflight refuses to
measure on CPU2 at only 33% idle. A follow-up selects an idle physical core and
its idle sibling without changing any other service, pins both original fixed
baseline and candidate to the same selected core, and reruns all 11 default
cases, 21 balanced pairs and five warmups. The follow-up exits 0: all cases
pass, with a maximum ratio of approximately 1.08 for function calls (95%
interval 1.069–1.077); GC traversal is approximately 1.004. This is within the
fixed gate, not a claim of restored call performance. Concurrent soak
qualification must still pass before deployment.

The initial v4 ASGI probe accidentally began before its link completed and is
excluded; the successful v4b reproduction starts after build completion. Raw
failed, incomplete and refused observations are retained separately.

## Remaining integration requirement

Native `gc.enable()` and threshold settings currently do not trigger automatic
collection (`get_count()` is a stub). Fixing explicit collection alone cannot
bound a long-lived API. The API owner supplies a separately tested,
XLang3-only lifespan maintenance task with cancellation and shutdown joining;
CPython automatic GC remains unchanged. It was not enabled on the old runtime.
Its unchanged helper has also been tested on this native candidate:
the actual default 30-second timer reclaims the 28,000 objects from 1,000
JSONResponses, and both normal and exceptional context exits cancel and await
the owned task without leaving a named GC task behind. The first probe failed
only because the native CLI did not automatically add its script directory to
the import path; its corrected probe, not the helper, adds that path.
Concurrent ASGI/collection qualification and coordinated Azure deployment are
complete. The helper is now enabled at its original 30-second interval on the
qualified V5 runtime; final-source live memory/latency acceptance remains
separate. This change does not claim complete native/finalizer/weakref cycle
parity or automatic-GC semantics.

## Live interval: memory gate failed

The final-source interval runs 2,700.30 seconds on core PID 892287 and API PID
896837, both with zero restarts. It completes 10,704 signed RPCs and 2,662 real
HTTP health requests with zero errors, one cached proxy, and all checker
threads joined. The API source is `4a843fe8`, app SHA-256 `4430304a...`; native
runtime is the qualified `b06aa789...`. This interval is complete, not an
interrupted or combined observation. The earlier GC-only interval was
interrupted for an authorized API source rollout and is not counted as a full
qualification.

The unchanged memory criterion compares final RSS with the first sample after
300 seconds and requires less than 20 MiB growth for **each** process:

| Process | RSS after 300 s, KiB | Final RSS, KiB | Sampled peak, KiB | Final growth, KiB |
| --- | ---: | ---: | ---: | ---: |
| Core | 144,436 | 160,868 | 160,868 | 16,432 |
| API | 283,416 | 312,844 | 325,432 | **29,428** |

The API exceeds the original limit. The checker exits 1 with `passed: false`;
the strict passing collector is not run. A separate fixed-source/PID failure
receipt explicitly records `memory_convergence_certified: false`. No threshold,
collection period, final sample timing or server collection is changed to make
the result pass. A new source/PID interval cannot replace this failure.

Call latency does not progressively stall in this observation. First/last five
minutes have RPC median/p95 5.180/14.078 ms and 5.704/8.764 ms; HTTP median/p95
12.038/24.063 ms and 12.148/17.594 ms. The complete report retains maxima and
every RSS sample. Concurrent public Chat/Search qualification, a separate
private metrics/calendar canary, and a bounded read-only scheduler sampler
around END are recorded background load, not merged into the stream samples.

START and END each complete two serial requests per model with the same prompt,
64-token budget, actual final usage, no warmups and no retries. HTTP first
content times are:

| Model | START, seconds | END, seconds |
| --- | --- | --- |
| 2B | 0.506, 0.405 | 0.571, 0.381 |
| 27B | 1.415, **8.925** | 1.252, **4.655** |

These are client-observed HTTP timings, not native GPU decode measurements.
Both 27B tails are retained and unresolved. Actual 5090 receipts for the two
START requests report native TTFT 0.225/0.307 seconds and decode 1.433/1.433
seconds. Existing dispatch logs are consistent with a delay before native
execution, but do not independently prove the resource-snapshot cause.

A read-only 65-second `/proc` sample records API RSS moving
291,596 -> 305,584 -> 292,048 KiB, including an approximately 30-second high
plateau. CPU ticks increase at both transitions. Correlation with maintenance
timing does not prove the allocation owner or establish convergence. Separate
10,000-iteration controls for ordinary tuple literals/construction, dict item
iteration, enumerate, zip and argument packing leave zero additional tuples
under V5; they do not rule out HTTP/async lifecycle retention.

Before the authorized subsequent source/core maintenance, admission is gated
and drained. An owned debugger preserves the old API heap in a private
0700/0600 diagnostics location under Cantor storage and detaches, with API PID
unchanged. The heap itself is not published. The following Cantor-only
resource-broadcast update (`2fa099f1`) is separate from this memory finding and
does not change this runtime or certify a fix for the remaining API growth.

Remaining work is to identify and reproduce the residual full-service memory
retention, fix its owner, and repeat a complete unchanged-source qualification.

Live evidence: [failed interval, exact identities and raw START/END samples](data/azure-native-gc-20261030-failed-observation.json),
[read-only RSS/CPU series](data/api-rss-working-set-20261030.json),
[isolated tuple allocation controls](data/tuple-retention-control-20261030.json).

## Evidence

- [Original runtime serial ASGI growth](data/function-gc-asgi-original-collect-v1.log),
  [serial candidate reclamation](data/function-gc-asgi-candidate-collect-v4b.log),
  [rejected long-soak record](data/function-gc-threaded-v4-failure.json),
  [accelerated crash stack](data/function-gc-gdb-repro-v4.log).
- [Old native guard control](data/gc-serialization-control-interpreter-test.log),
  [V5 guard checks](data/gc-serialization-candidate-v5-test.log),
  [accelerated V5 diagnostic](data/gc-serialization-repro-v5.json).
- [V5 native checks](data/function-gc-native-correctness-v5.log),
  [full fixture comparison](data/function-gc-fixture-differential-v5.json),
  [asyncio contracts](data/function-gc-asyncio-accelerator-v5.log),
  [ABI/layout](data/function-gc-abi-layout-v5.json),
  [complete fixed gate](data/function-gc-performance-gate-v5.json),
  [idle admission](data/function-gc-idle-preflight-v5.json),
  [real native lifecycle checks](data/function-gc-native-lifespan-v5.log),
  [900-second concurrent qualification](data/function-gc-threaded-soak-v5.json).
- The retained V4 [inconclusive gate](data/function-gc-performance-gate-v4.json),
  [refused busy-core preflight](data/function-gc-idle-preflight-v4.json),
  [selected idle-pair receipt](data/function-gc-idle-pair-preflight-v4.json) and
  [passing full follow-up](data/function-gc-performance-gate-v4-idle-pair.json)
  remain separate from V5 and do not override V4's later concurrency failure.
