# Compiled eval on the active VM stack — 2026-10-10

Genshi calls `eval(code, globals, locals)` repeatedly with a compiled eval-mode
code object and live dictionaries. The synchronous `builtin_eval` path starts a
new `Interpreter` and VM loop for each call. This trial adds an ordinary-`CALL`
fast path for that exact shape: the existing compiled entry function is pushed
onto the current VM stack. Source strings, custom mappings, other code modes,
keywords, expanded arguments, and observer-enabled calls keep the existing
synchronous implementation. The implementation is a generic runtime path; it
does not reimplement Genshi or another pure-Python library.

Explicit eval locals now have an owning scope token. The synchronous evaluator
holds the token until `run_code_object` returns; the VM frame can hold the same
token across nested calls, exceptions handled inside the frame, and debugger
pauses. Frame cleanup releases it on normal return or unwind. This is required
because eval-local name lookup must remain scoped to the eval entry function.

## Results

| Genshi pyperformance case | XLang3 before trial | XLang3 candidate | CPython 3.14.7 candidate | Candidate / CPython |
|---|---:|---:|---:|---:|
| `genshi_text` | 333 ± 21 ms | 306 ± 7 ms | 19.6 ± 0.6 ms | 15.6× slower |
| `genshi_xml` | 1.03 ± 0.19 s | 865 ± 76 ms | 42.6 ± 1.0 ms | 20.3× slower |

The pre-trial and candidate XLang3 runs are independent fast-mode runs, and
pyperf warned that both XLang3 results may be unstable. The 8% text and 16% XML
reductions are directional only, not a paired speedup claim. The candidate
compiled-eval phase diagnostic measured 39.0 ms per 10,000 calls, compared with
58.6 ms in the earlier diagnostic record; that single-pass comparison is also
directional. The remaining rendering gap is still large.

A serial rigorous-mode rerun measured `genshi_text` at 300 ± 15 ms and
`genshi_xml` at 2.00 ± 1.57 s; pyperf marked both unstable. The XML variance
makes this run unsuitable for comparison against the fast-mode parent. A
five-process phase probe of a follow-up attempt to remove weak-pointer locking
from eval-local lookup measured compiled eval at 40.3 ms per 10,000 calls
(median; 40.0–40.8 ms). That attempt showed no improvement over the earlier
39.0 ms pass and was reverted.

## Next runtime cost found

In the current eval frame, each global/name load asks the thread-local eval
scope registry, copies the locals `Value`, constructs a temporary string
`Value`, and calls generic mapping dispatch. The frame path makes this common
in Genshi because its locals are exact dictionaries. A follow-up source change
uses the existing string-key dictionary lookup for exact dict locals and keeps
the generic path for custom mappings. The active executable was built before
that lookup change and before the observer-check reorder below; neither source
change is represented by this run. At build time, the executable came from a
dirty working tree that differs from the October 9 full-suite source and
binary: the tree had 41 modified tracked files plus untracked runtime work,
and the fixed-path executable/runtime hashes are
`8386E476AE05C879DC490F60DB364E30A9F5189085767DEAB36F57A16EF8A914` /
`1A926F4635852CDFD87AABEED65987A7B5F700DC144BAFF845D616AEC8155F1C`. The
October 9 report used `5997b264f8b71c38b57c98763788702f38146217` and hashes
`8a1c1936d7d765a6e14bc53106219ed24394893dcd04524c3e24224129f59545` /
`aacc20cd899e519dc19c67160a0edc2e873146671ce42286589fd9236f7d9a77`. Treat
the async-tree difference as a real observation for this build, but not as a
causal eval-frame regression until the other source changes are isolated.

## Validation and limits

The fixed Release build succeeded through the established VC environment
controller, and the complete Python 3.14.7 fixture suite exited successfully.
The unchanged official case names `genshi_text` and `genshi_xml` completed for
both XLang3 and CPython 3.14.7. The complete pyperformance suite and the fixed
baseline regression gate have not completed for this candidate. The complete
97-case fast-mode run is in progress; its live log is
[here](data/pyperf-full-compiled-eval-frame-r1-xlang3-20261010.log). The
suite-level JSON will be available after the runner completes. This is not a
performance checkpoint or a commit-ready result.

At the latest recorded checkpoint, the live run has reached case 22 of 97,
`asyncio_tcp_ssl`; the suite JSON has not been written yet. Cases 1–5 completed
with these reported means:
`2to3` 2.62 s, `argparse`'s `many_optionals` 8.63 ms,
`argparse_subparsers`'s `subparsers` 131 ms, `async_generators` 10.2 s, and
`async_tree_none` 18.5 s. Cases 8–10 reported `async_tree_eager` 4.28 s,
`async_tree_eager_cpu_io_mixed` 13.4 s, and
`async_tree_eager_cpu_io_mixed_tg` 19.6 s. Pyperf marked all completed
measurements unstable. Case 13, `async_tree_eager_memoization`, completed at
16.8 ± 6.3 s and was also unstable. Case 14,
`async_tree_eager_memoization_tg`, hit the 600-second cap.
Cases 6, 7, 11, 12, 14, 16, 17, and 18 (`async_tree_cpu_io_mixed`,
`async_tree_cpu_io_mixed_tg`, `async_tree_eager_io`, and
`async_tree_eager_io_tg`, `async_tree_eager_memoization_tg`, `async_tree_io`,
`async_tree_io_tg`, and `async_tree_memoization`)
each hit the 600-second full-case cap; the runner continued after each timeout.
These are incomplete benchmark cases, not timing results. Case 15,
`async_tree_eager_tg`, completed at 14.5 ± 0.9 s, with pyperf warning that the
measurement was unstable. Case 19, `async_tree_memoization_tg`, completed at
5.38 ± 0.10 s and pyperf also marked it unstable. The live log is the
authoritative progress artifact. Case 20, `async_tree_none_tg`, completed at
3.77 ± 0.30 s and was also marked unstable. Against the saved CPython 3.14.7
reference of 239.4 ms, that is about 15.8× elapsed time; this XLang3 result is
directional because pyperf marked it unstable and the runs are unpaired. The
suite-level JSON is written only after the run completes. Case 21,
`asyncio_tcp`, completed at 3.79 ± 0.15 s, also unstable; against the saved
585.3 ms CPython result it is about 6.5× elapsed time, again directional.

## Regression source audit (unproven)

The current source review found two avoidable checks on common execution paths
that need paired measurement after this suite releases the fixed executable.
The main VM loop reads `native_trace_event_may_dispatch()` on every opcode
([xlang_vm_loop.cpp](../../src/executor/xlang_vm/xlang_vm_loop.cpp)); even when
native tracing is disabled, this performs a relaxed atomic load. The compiled
eval call-site guard also looks up the canonical `eval` builtin and checks
observer/event state for every call with exactly three positional arguments
([xlang_vm_ops_call.h](../../src/executor/xlang_vm/ops/xlang_vm_ops_call.h)).
Most such calls are ordinary calls, so the observer probe currently runs
before rejecting their callee, and a builtin-map lookup runs before the call
is identified as `eval`. The source now filters for a native function named
`eval` first, confirms canonical builtin identity, and puts observer checks
behind that guard. This change is not in the active
Release executable and has not been compiled or measured yet; the running
suite still tests the prior binary. A `global_three_arg` case was added to
[`profile_python_call_shapes.py`](../../benchmarks/diagnostics/profile_python_call_shapes.py)
to measure this call shape against its loop and one-argument controls once the
fixed executable is free. Either this overhead or the per-opcode
native-trace check could affect async-tree and other call-heavy benchmarks,
but neither has been isolated yet.

The async opcode diff itself only adds native-trace suspension flags around
generator returns; it does not change normal await scheduling. That makes the
per-opcode VM-loop check a more direct candidate than the async handler diff,
but the full executable contains many unrelated working-tree changes and the
current run is unpaired. Treat both findings as experiment candidates, not a
root-cause conclusion.

The full run's `async_tree_none` result was 18.5 ± 1.7 s, versus 4.416 s in the
[October 9 full comparison](pyperformance-xlang3-python-new-r4-vs-cpython3147-full-fast-20261009.md)
built from commit `5997b264`. This is a large
regression signal, although the runs are unpaired and their executable hashes
differ. It is separate from the IO-tree timeout: the October 9 report also
timed out on both `async_tree_io` variants. The newer GC graph-discovery code
is unlikely to explain the `none` slowdown: source inspection shows the plain
cycle scan is reached only through explicit `gc.collect()`, and this benchmark
does not call it.

The strongest broad-path candidate remains the compiled-eval guard present in
the active executable. Its original form performs debug, trace, profile,
monitoring, and pending-event probes for every exact three-positional-argument
`CALL` before confirming the callee is `eval`. The current source moves those
checks behind the native `eval` name and canonical builtin identity; that
source change postdates the active executable. The historical async-tree
profile counted about 1.16 million Python calls, but did not count three-arg
`CALL`s, so this is a plausible cause rather than a measured attribution. The
full run's `async_tree_none` score is the control for a fixed-path build with
this guard ordering. The timer-comparison path remains a separate candidate
for IO cases and cannot by itself explain the `none` result.

The IO tree has a separate likely hot path. Each of its 46,656 leaf sleeps
schedules a `TimerHandle`; XLang3 imports its native `_heapq`, but
[`HeapAccess::less`](../../src/runtime/modules/system/heapq_module.cpp) still
routes object ordering through `runtime_value_compare`. For Python instances,
that resolves and calls `__lt__`; the generic callable path enters an
`Interpreter` unless the method matches one of a few unrelated inline shapes.
`asyncio.events.TimerHandle.__lt__` is a pure-Python `_when` comparison, so
large timer heaps repeatedly cross back into the VM during sift operations.
Historical unscored original-body runs measured 35.44 s with Python `heapq.py`
fallback ([raw result](data/before-heapq-original-io-d-control-20261008.json))
and 10.19 s with native `_heapq`
([raw result](data/heapq-native-original-io-body-20261008.json)), while this
current full-suite binary timed out on `async_tree_io` at 600 s. These are
different builds and run boundaries, so they do not quantify the current
comparator cost. The new source comment records the required fast-path rule:
prove the exact class/comparator and retain generic behavior for subclasses and
mutations. Since `_heapq` is a CPython native module, optimizing that native
module keeps Python `heapq.py` unchanged and follows the project's
native-module rule. A count-only diagnostic at
[`async_tree_timer_compare_count.py`](../../benchmarks/diagnostics/async_tree_timer_compare_count.py)
is ready to compare the number of `TimerHandle.__lt__` calls under the two
runtimes after the full suite releases the machine; its elapsed time is
explicitly unscored because the wrapper changes the call path. It also accepts
`--levels 1` through `--levels 6` so a smaller tree can validate the diagnostic
path before attempting the full 46,656-leaf run.

The CPython 3.14.7 source comparison narrows the likely gap without claiming
that CPython hard-codes asyncio timers: its native `_heapq` sift operations call
`PyObject_RichCompareBool(a, b, Py_LT)` ([pinned 3.14.7 source, sift code](https://github.com/python/cpython/blob/v3.14.7/Modules/_heapqmodule.c#L1940-L1983)).
XLang3's equivalent comparison currently enters `runtime_value_compare`, which
performs special-method resolution through `object_get_special_method` and
`class_get_bound_attr`, creates a bound method, invokes the generic callable
path, and converts the result to truth. The MRO vector is cached, but
`class_lookup_attr` still scans it for each comparison. This makes repeated
class-method lookup, bound-method creation, and Python-call setup the specific
runtime costs to count and profile against CPython's type rich-comparison slot
dispatch. The current evidence makes this a strong candidate for the IO-tree
slowdown, not yet a measured attribution; the next run must first count timer
comparisons and then isolate lookup/call overhead before changing semantics.

An independent allocation/GC path is also a serious candidate for memory
pressure and runtime cost. `gc_track_object` and `gc_untrack_object` each take
the process-wide tracked-object mutex around vector updates
([gc_module.cpp](../../src/runtime/modules/system/gc_module.cpp#L63)). They
are called by most container, instance, generator, and iterator constructors
and destructors. The same source shows `gc.get_count()` always returns
`(0, 0, 0)`, while configured thresholds are only stored and read; collection
is invoked explicitly through `gc.collect()` and no allocation hook checks
those thresholds. Thus XLang3 currently lacks CPython-style automatic
cyclic-GC triggering despite `gc.isenabled()` defaulting to true. This can
leave cycles alive throughout a workload and adds a global lock to tracked
allocations. The multi-gigabyte asyncio snapshots are consistent with that
hypothesis but do not prove that the benchmark retains cycles or that GC
explains its timeout. After the full run, a diagnostic should compare tracked
object counts and memory before and after explicit collection on the exact
async-tree body, then measure registry-lock cost on a high-allocation control.
Any automatic collector or registry redesign needs separate correctness and
performance validation; changing GC behavior inside the official benchmark
would invalidate its score. The new
[`async_tree_gc_retention_probe.py`](../../benchmarks/diagnostics/async_tree_gc_retention_probe.py)
uses the official benchmark class, supports reduced levels and TaskGroup, and
records tracked-object counts and process memory before and after explicit
collection. It passes a Python 3.14.7 syntax check; its diagnostic execution
is deferred until the full suite finishes so it cannot perturb official scores.

The key call-boundary difference is now confirmed in source: `_heapq` invokes
the comparison from native C++, so it does not execute at an XLang VM `CALL`
instruction and cannot reuse that instruction's `CallSiteCache` or method
inliners. `runtime_call_callable` has only generic trivial-function and
captured-item shortcuts before constructing an `Interpreter` and calling
`run_function_value`; `TimerHandle.__lt__` has an `isinstance` guard and two
attribute reads, so it misses those shortcuts. CPython's `_heapq` likewise
enters through a native rich-compare API, but then dispatches the type slot and
runs the warmed method in its adaptive evaluator. This identifies repeated
interpreter entry from a native comparison callback as the leading structural
root-cause candidate. Counts and a focused native-callback A/B are still needed
before claiming the magnitude or selecting a specialization.

The source candidate added per-class raw-function caches for the
`__lt__` and `__gt__` legs used by `<`. It stores non-owning `FunctionObject*`
targets guarded by the receiver class version, then enters the same Python
function with explicit `(self, other)` arguments. That preserves the Python
frame and normal `NotImplemented` handling while skipping repeated MRO scans
and temporary bound-method allocation on warmed raw-function comparisons.
Descriptors and native functions continue through generic binding. The cache
stores no pointer into the class `attrs` map: implicit annotation materialization
can rehash that map without changing the rich-comparison target. The focused
`heapq_rich_compare_cache` fixture covers reflected fallback, ignored instance
shadows, non-owning cache lifetime, inherited replacement and deletion,
mid-operation method replacement, and descriptor fallback. The source was
compiled into the fixed Release executable at 13:58, before this full run began
at 13:59; the `object_model.cpp` and `heapq_module.cpp` object files were
written at 13:57. Thus the active run exercises this cache. Its eager I/O cases
11 and 12 both hit the 300-second cap, but there is no same-cap source-matched
control, so those outcomes neither establish nor reject a speedup. The fixture
passes CPython 3.14.7; XLang3 fixture validation remains pending until the full
run releases the executable. This is still not a measured performance win.

The `[17/97] async_tree_io_tg` worker also showed high transient memory use:
process 11864 sampled at about 3.16 GB private memory, then at 1.23 GB in a
later sample 92 seconds after its start, while still consuming CPU. The case
then hit its 600-second full-case timeout. These are live snapshots, not a
measured peak or evidence of a leak. The workload creates 55,986 child tasks,
so suspended-frame and per-frame cache storage are worth measuring alongside
timer-comparison cost. During `[19/97] async_tree_memoization_tg`, worker
process 36648 was sampled at 928.6 MB working set and 12:02 local time, about
18 seconds after that worker started. This is another live snapshot, not a
peak measurement. A later worker for the same case (PID 38136) was sampled
within seconds of launch at 1.25 GB working set and 1.42 GB private bytes; it
was still live at the capture. Case 19 then completed with its unstable
5.38 ± 0.10 s timing. These transient samples show high allocation pressure
during a task-group sample, but do not identify a leak or explain the separate
case 18 timeout. In case 20, `async_tree_tg`, another worker (PID 32888) was
sampled about 13 seconds after launch at 1.25 GB working set and 1.41 GB
private bytes. Similar memory use across distinct task-group variants makes
per-frame storage a stronger measurement target, but still does not establish
causation. The full run later completed case 21 (`asyncio_tcp`) at 3.79 ± 0.15 s
and entered case 22 (`asyncio_tcp_ssl`) at 12:07 local time. That SSL case hit
the runner's 600-second timeout at 12:17. Case 23 (`asyncio_websockets`)
completed at 474 ± 10 ms; the existing CPython 3.14.7 full reference is 176.4
ms, or about 2.69× faster (cross-run comparison). At 12:17, the controller
advanced to case 24 (`base64`), which is now running. The full-suite run remains
active. No full-suite JSON exists until the run ends.

Source inspection found a concrete per-frame memory multiplier in the active
binary's `XlangVMInstrCacheStorage::reset`: every activation allocates a dense
4-byte cache-slot index and a 16-byte monitoring state for every IR
instruction, including uncached instructions. That is at least 20 bytes per
IR instruction per live frame before specialized cache payloads, locals,
registers, or object overhead. The working-tree candidate keeps the 4-byte
cache-slot map but allocates the 16-byte monitoring states only when a
monitored event reaches a frame. This is safe for the ordinary path because
`emit_monitoring_event` checks the frame's active event mask before asking for
per-location disable state. The existing `sys_monitoring_all_events` fixture
is now registered in both fixture runners; it exercises dynamic enablement and
`DISABLE` restart. The active fixed executable predates these edits, so the
running suite does not measure them. The large live process snapshots make
lazy allocation a priority to test, but do not prove these arrays caused the
sampled memory use or the timeouts. After the official run, validate the
fixture, then compare eager and lazy allocation builds from the same source
state on task-group cases. The fixed executable remains the full-suite
reference; this A/B isolates the new change from unrelated working-tree edits.
Sharing the remaining index/cache state at function-code scope would need
separate ownership and reentrancy analysis.

The monitoring fixture now also enables global `LINE` events from inside a
running function and checks that the remaining three line locations in that
same activation are reported. Running the fixture with `C:\Python\Python314\python.exe`
produced the expected three callbacks. This specifically checks the lazy
vector's first allocation during an active frame, in addition to enabling
monitoring before a later activation. The XLang3 result is pending a rebuilt
candidate; the active fixed executable cannot validate this source edit.

This candidate leaves the 424-byte owning cache records, cache-site indexing,
and their access path unchanged. The earlier [cache-storage trials](vm-cache-storage-trials-20260930.md)
changed that storage/materialization design and measured `unpickle_pure_python`
1.01–1.02% slower; those results remain a reason not to broaden this edit into
another cache-payload rewrite. The present test isolates only the otherwise
unused monitoring-state vector on async task-group workloads.

The CPython 3.14 source comparison points to a broader call-path experiment:
CPython specializes exact-argument Python calls as `CALL_PY_EXACT_ARGS` and
pushes the frame directly. At the start of this suite, XLang3 cached ordinary
`CALL` targets as `UserFunction`; `ExactPositionalFunction` was assigned only
in `CALL_METHOD`. The current source now stages an ordinary-`CALL` exact
positional entry: it caches the proof only for synchronous fixed signatures,
excludes functions served by the captured-item fast path, and invalidates the
proof on code-version changes. The first draft had a cache-hit branch but did
not install the cache kind; that unreachable draft was corrected. A matching
fixture covers target changes, `__code__` replacement, trace events, defaults,
variadic functions, and generators. The call-shape diagnostic also includes a
tiny closure call whose captured free variable prevents the existing trivial
and argument-binary inliners from hiding ordinary frame-entry cost. These source
changes are newer than the running suite and are not in its fixed Release
binary. Their ability to reduce hot-call cost, including whether frame
argument transfer offsets the added cold-site checks, remains unmeasured. See
the existing [CPython 3.14 VM comparison](cpython314-vm-comparison-20260930.md).

An earlier instrumented official `async_tree_none` profile recorded 1,157,378
Python asyncio call events under XLang3 and 1,148,050 under CPython 3.14.7,
with the same dominant functions (`get_debug`, `_check_closed`, `call_soon`,
`Handle.__init__`, and `Handle._run`). That near-equal call count argues against
a large increase in Python-level fallback calls; it points toward higher cost
per dispatch, object operation, or native boundary. The profile is from the
October 6 `build-repro/Release` executable, not the active fixed-path build,
and profiling changes timing, so it is structural evidence only. See the
[XLang3 profile](data/async-tree-official-call-profile-xlang3-20261006.txt)
and [CPython profile](data/async-tree-official-call-profile-cpython314-20261006.txt).

The initial guard bypasses native C-call event handling only when no tracing,
profiling, monitoring, debug hook, pending interpreter event, or debug poll can
dispatch at the call site. Calls that begin with observers enabled decline to
the old implementation. Observer activation from inside the eval body and
debugger pause/resume behavior still need focused parity tests before widening
the fast path.

The active full run reached case 25 (`bpe_tokeniser`) at 12:25 local time on
October 10 and was still running at 12:33; pyperf was repeatedly launching
workers, so the process was live rather than stalled. At 12:33, the current
worker had accumulated about 69 seconds of CPU for `--loops 1`, with no case
result emitted yet. At 12:35, the runner recorded `Full-case timeout:
bpe_tokeniser exceeded 600 seconds` and advanced to case 26 (`chameleon`).
The benchmark's Python source trains a 1,024-entry vocabulary using nested pair scans, `Counter`,
`zip`, comprehensions, and repeated list reconstruction, then encodes the input.
That makes this a broad interpreter workload, unlike the TimerHandle comparison
path; the proposed rich-comparison cache cannot fix it. Existing evidence shows
the instability in this case: the October 9 full report timed out, while a
separate October 7 targeted run completed at 26.0 s. The CPython 3.14.7
reference recorded on October 1 is 3.863 s. These are cross-run results, not a
paired comparison; the active October 10 run also timed out at 600 seconds.
The timeout therefore remains a separate priority for interpreter-wide
hot-loop/call overhead work. See the [unchanged pyperformance 1.14.0 benchmark source](data/bpe-tokeniser-official-source-1.14.0-20261010.py),
the [October 7 XLang3 run](data/vm-captured-lookup-validation-20261007-official-bpe.log),
the [CPython reference](pyperformance-cpython314-shared-deps-full-fast-20261001.md),
and the [October 9 full comparison](pyperformance-xlang3-python-new-r4-vs-cpython3147-full-fast-20261009.md).
Case 26 (`chameleon`) then completed at 140 ± 4 ms in the October 10 run. The
October 9 paired report measured 139.4 ms for XLang3 and 11.6 ms for
CPython 3.14.7 (12.02× slower); this new cross-run result is consistent with
the prior XLang3 measurement. Case 27 (`chaos`) started at 12:35 local time and
is active. Chameleon remains a confirmed call-heavy slowdown. Earlier
profiling found 26,506 `Scope.get` calls and 16,002 `get_name` calls in one
render, but profiling disables optimizations and changes timing. The earlier
`super()` dispatch candidate reduced a Chameleon score from 178.068 ms to
158.564 ms while preserving render identity, yet remained far behind the
11.851 ms CPython reference. See the [call profile](data/chameleon-builtin-constants-xlang3-callprofile-20261007.log),
[super-call checkpoint](super-method-call-checkpoint-20261007.md), and
[official comparison](pyperformance-xlang3-super-method-call-vs-cpython3147-full-fast-20261007.md).
Those narrow lookup and receiver changes have already been tried; the pending
ordinary-`CALL` frame-entry candidate is a broader, distinct hypothesis.

Cases 27 (`chaos`) and 30 (`coroutines`) completed at 460 ± 13 ms and 151 ± 5
ms. The October 9 CPython 3.14.7 reference reports 48.17 ms and 17.41 ms,
respectively, so these are about 9.55× and 8.67× slower across runs. Case 29
(`concurrent_imap`) failed while pickling a multiprocessing worker: CPython's
`synchronize.py` passed `SemLock.handle` to `_winapi.DuplicateHandle`, which
raised Win32 error 6; the worker then saw EOF. Source inspection identified
the cause in XLang3's native `_multiprocessing.SemLock`: Windows was exposing a
synthetic incrementing registry key as `handle`, not a kernel handle. The
working-tree fix now creates unnamed Win32 semaphores, uses their handles for
acquire/release and duplicates, and closes each owned handle. This matches
CPython 3.14.7's Windows implementation, including its zero-time value probe
and per-wrapper ownership count ([CPython source](https://github.com/python/cpython/blob/v3.14.7/Modules/_multiprocessing/semaphore.c#L48-L77)).
A spawn fixture covers lock transfer to a child and acquisition after the
parent releases it. Neither the native fix nor the fixture has been built or
run in XLang3 yet; the fixed-path full baseline is still active. Case 40
(`fastapi`) emitted `fastapi_http: 5.43 ± 0.22 s`, and the runner advanced to
case 41 (`float`). Float completed at 77.5 ± 3.1 ms, consistent with the
October 9 paired XLang3 result of 77.45 ms against CPython's 55.2 ms (1.40×
slower). Case 44 (`generators`) measured 329 ± 13 ms versus the October 9
CPython result of 26.63 ms (about 12.35× slower across runs). Case 45
(`genshi`) measured `genshi_text` at 303 ± 6 ms versus 19.37 ms on CPython
(about 15.64× slower across runs). Its `genshi_xml` subtest measured 844 ± 13
ms, but the prior full comparison excluded that result for incorrect output;
do not score it until current output equivalence is checked. The suite is now
at case 46 (`go`).

## Evidence

- Build: [Release log](data/build-compiled-eval-frame-r1-retry-Release-20261010.log)
- Fixtures: run with `C:\Python\Python314\python.exe` and the fixed Release executable; exit code 0
- Genshi phase probe: [XLang3](data/genshi-compiled-eval-frame-r1-xlang3-20261010.json), [CPython 3.14.7](data/genshi-compiled-eval-frame-r1-cpython3147-20261010.json)
- Official pyperformance: [XLang3 JSON](data/pyperf-genshi-compiled-eval-frame-r1-xlang3-20261010.json), [XLang3 log](data/pyperf-genshi-compiled-eval-frame-r1-xlang3-20261010.log), [CPython JSON](data/pyperf-genshi-compiled-eval-frame-r1-cpython3147-20261010.json), [CPython log](data/pyperf-genshi-compiled-eval-frame-r1-cpython3147-20261010.log)
- Serial rigorous-mode attempt (unstable): [XLang3 JSON](data/pyperf-genshi-compiled-eval-frame-r1-rigorous-xlang3-serial-20261010.json), [XLang3 log](data/pyperf-genshi-compiled-eval-frame-r1-rigorous-xlang3-serial-20261010.log)
- Five-process eval phase probe of the rejected lookup tweak: [samples](data/genshi-compiled-eval-frame-r2-xlang3-samples-20261010.jsonl)
- Prior comparison data and cache trial: [eval call-site investigation](eval-callsite-cache-investigation-20261010.md)

## Full-suite result and rebuilt candidate

The October 10 full run completed all 97 definitions and wrote the [JSON](data/pyperf-full-compiled-eval-frame-r1-xlang3-20261010.json)
and [log](data/pyperf-full-compiled-eval-frame-r1-xlang3-20261010.log). It
recorded 21 failures or timeouts, so 76 definitions completed. This binary
predated the later call-path and Windows semaphore changes. Its executable and
runtime SHA-256 values were
`8386E476AE05C879DC490F60DB364E30A9F5189085767DEAB36F57A16EF8A914` and
`1A926F4635852CDFD87AABEED65987A7B5F700DC144BAFF845D616AEC8155F1C`.

The largest broad regression signal was `async_tree_none` at 18.5 ± 1.7 s,
compared with 4.416 s in the October 9 XLang3 capture and 229.3 ms on CPython
3.14.7. That was an unstable, cross-run comparison; it points to work that
needs isolation, not a measured cause. The run also showed `json_dumps` at
31.7 ± 0.3 ms versus 7.636 ms on CPython (4.15× elapsed), and Genshi text at
303 ± 6 ms versus 19.37 ms (15.6× elapsed, cross-run). Partial subtests and all
definition outcomes are retained in the log and JSON.

The fixed Release path was rebuilt without reconfiguration after the run.
The `multiprocessing_spawn_semlock.py` regression fixture now passes on that
binary, confirming that a child can receive and use the real Windows semaphore
handle. A full fixture sweep stopped at an unrelated `sys_monitoring_all_events`
expected-output mismatch (`monitoring-mid-frame-result` returned `3 4`, expected
`3 3`); it needs separate investigation before the suite can be called green.

The post-build call-shape diagnostic still shows a broad call-dispatch gap. In
a sequential XLang3/CPython 3.14.7 run, 50,000 global one-argument calls took
3.838 ms versus 1.327 ms (2.89× elapsed); three-argument calls took 5.007 ms
versus 1.434 ms (3.49×); closure calls took 20.425 ms versus 1.789 ms (11.42×);
and bound methods took 17.216 ms versus 1.391 ms (12.38×). See the [XLang3
samples](data/call-shapes-r3-fixed-sequential-xlang3-20261010.json) and
[CPython samples](data/call-shapes-r3-cpython3147-sequential-20261010.json).
These measurements localize a substantial part of the remaining gap to VM call
and frame entry, especially closure and bound-method dispatch. They do not show
that the overall performance goal has been achieved. The source changes under
test are VM/runtime work; the IR optimizer was not changed for these call-path
experiments.

The rebuilt-binary full pyperformance run is in progress at
`base64` (case 24 of 97). Cases 11, 12, 16, 17, and 22,
`async_tree_eager_io`, `async_tree_eager_io_tg`, `async_tree_io`, and
`async_tree_io_tg`, and `asyncio_tcp_ssl` exceeded the 300-second case cap and
the runner continued.
Its [live log](data/pyperf-full-callpath-candidate-xlang3-20261010.log)
currently records `async_tree_none` at 4.49 ± 0.55 s; the corresponding
CPython 3.14.7 reference is 229.3 ms. The new result is close to the earlier
4.416 s XLang3 capture and far below the unstable 18.5 s sample from the
compiled-eval-frame run. Treat the latter as an outlier signal, not as evidence
that the newer VM call path fixed async-tree performance. The new run also
records `async_tree_eager_cpu_io_mixed_tg` at 4.74 ± 0.18 s versus 385.4 ms in
the CPython reference, close to the older XLang3 capture of 4.623 s. The task
group and non-task-group variants remain about 12× elapsed time. The full run
still needs to reach all 97 definitions, including JSON and the remaining
async-tree variants. Do not compare the pre-build full suite with the newer
binary as if they were paired.

Case 13 completed at 3.41 ± 0.05 s versus 187.5 ms in the saved CPython 3.14.7
reference (about 18.2× elapsed time). Pyperf marked the XLang3 result unstable;
the ratio is directional and cross-run, not a paired score.
Case 14 completed at 5.38 ± 0.12 s versus 266.0 ms in the same reference
(about 20.2× elapsed time); pyperf also marked it unstable.
Case 15 completed at 3.33 ± 0.06 s versus 193.1 ms (about 17.2× elapsed
time), also unstable. The repeated task-group/eager variants confirm that
these costs remain large in the executable that includes the raw-function
rich-comparison cache. Both non-eager I/O cases and both eager I/O cases hit
the cap in this run. There is no same-cap source-matched control, so the
cache's contribution remains unknown.
Case 18, `async_tree_memoization`, completed at 5.23 ± 0.11 s versus 278.5
ms in the saved CPython reference (about 18.8× elapsed time); pyperf marked
the XLang3 result unstable.
Case 19, `async_tree_memoization_tg`, completed at 5.59 ± 0.34 s versus
275.8 ms (about 20.3× elapsed time), also unstable.
Case 20, `async_tree_none_tg`, completed at 3.65 ± 0.08 s versus 239.4 ms
(about 15.3× elapsed time); its XLang3 measurement was unstable as well.
Case 21, `asyncio_tcp`, completed at 3.84 ± 0.09 s versus 585.3 ms in the
saved CPython reference (about 6.6× elapsed time). Pyperf marked it unstable.
Case 22, `asyncio_tcp_ssl`, hit the 300-second cap. Its worker used about
126 MB working set and 110 MB private memory shortly before timeout, so this
case did not show the multi-gigabyte pressure seen in async-tree workloads.
The timeout indicates a separate CPU/protocol-path bottleneck; it is not
attributed yet.
Case 23, `asyncio_websockets`, completed at 495 ± 22 ms versus 176.4 ms in
the saved CPython reference (about 2.8× elapsed time); pyperf marked the
XLang3 measurement unstable.

The run's [provenance record](data/pyperf-full-callpath-candidate-xlang3-20261010-provenance.json)
pins the CPython 3.14.7 manager, Release binary, key VM source files, and runner.
After this run began, the working tree gained a closure-frame reuse candidate:
`XlangVMFrame::set_closure` now keeps the existing vector and capacity while
copying captured values into it. The candidate avoids allocating a new vector
object and backing storage for each nonempty closure call. The [regression
fixture](../../tests/fixtures/core/closure_frame_reuse.py) exercises alternating
closures and a temporary closure at one call site. This source change is not in
the running executable or its full-suite measurements; build it and compare the
call-shape and async-tree cases after the active full run finishes.

At 14:27 local time, the `async_tree_eager_io_tg` XLang3 worker had about
2.06 GB working set and 2.56 GB private memory while using 44.9 CPU seconds.
This is one process snapshot during a capped benchmark, not a peak or leak
measurement. It reinforces measuring per-frame storage and timer-heap
comparison costs separately; it does not show which one caused the memory or
time cost. The new snapshot used the unchanged binary hash recorded in the
provenance file.

At 14:36, the `async_tree_io` worker reached 2.87 GB working set and 2.98 GB
private memory. This is another live snapshot, not a peak. The lazy monitoring
state allocation and closure-vector reuse in the current `xlang_frame.h` were
edited after the 13:58 build (the header hash also differs from the run
provenance), so neither optimization is represented by these memory readings.
The next fixed-path candidate build must measure whether lazy monitoring state
reduces this large per-frame footprint while keeping the monitoring fixtures
green.

The `async_tree_io` case later hit its 300-second cap. At 14:38, one of its
workers was observed at 3.82 GB working set and 3.96 GB private memory. The
case produced no timing result; this sample documents memory pressure only.
Case 17, `async_tree_io_tg`, later hit its 300-second cap. At 14:41 one worker was
observed at 4.49 GB working set and 4.69 GB private memory after about 6.3 CPU
seconds. This is a transient sample, not a peak, but it shows the task-group
I/O case can allocate several gigabytes early in a worker run.
At 14:43 a later worker in the same case was at 3.65 GB working set and 3.84
GB private memory. Repeated worker snapshots vary substantially, so these are
not a stable per-run peak or leak measurement.
Case 20, `async_tree_none_tg`, has no simulated I/O or timer heap. At 14:51 one
worker was observed at 0.86 GB working set and 0.97 GB private memory after
3.7 CPU seconds. This shows substantial memory use also occurs in the
non-I/O task-group path, so TimerHandle comparisons cannot explain all of the
allocation pressure; task/frame storage and cyclic retention remain separate
candidate causes.
