# Async tree I/O slowdown: measured root-cause candidate

## Result

The full fast-mode pyperformance run on 2026-10-10 attempted all 97 definitions. It completed 78 and recorded 19 failures/timeouts. On 88 matched successful subtests, the geometric mean of CPython 3.14.7 time divided by XLang3 time was 0.12834×. The full comparison and raw results are in [the run report](pyperformance-xlang3-current-vs-cpython3147-full-fast-20261010-r2.md).

The async-tree I/O diagnostic points to expensive Python rich-comparison callbacks invoked from native `_heapq`. At six levels, the official workload body made exactly 714,793 `TimerHandle.__lt__` calls on each runtime. With a diagnostic wrapper counting those calls, XLang3 took 10.936 seconds and CPython 3.14.7 took 0.6395 seconds, about 17.1× less time on CPython. This is a diagnostic timing with instrumentation, not a pyperformance score; it isolates a path with equal call counts and a large per-call cost difference.

The call path is `heapq` → native XLang3 `_heapq` → `HeapAccess::less` → `runtime_value_compare` → Python `TimerHandle.__lt__`. XLang3 already caches the raw rich-comparison function by class version, but the generic Python-function branch still constructs an `Interpreter` and executes a frame for each callback. The ordinary VM call-site adaptive cache is not involved because `_heapq` calls back into Python from native code. CPython's native heap code enters its optimized evaluator for the same Python method.

The benchmark source fixes six recursive levels and six branches at each
level, yielding 46,656 leaf calls per body. Each `io` leaf awaits a 50 ms sleep;
the memoization variants avoid most of those waits by caching 90% of the data.
In the current fixed-binary rerun, the non-I/O, mixed, eager, and memoization
variants through case 15 completed in roughly 1.3–5.5 seconds, while all three
all-I/O variants attempted so far reached the 300-second cap. Together with
the 714,793 measured timer comparisons and the callback timings above, this
points to the cost of scheduling and resuming many Python tasks, including the
native-to-Python timer-comparison callback. It does not establish that the
comparison path alone explains the full timeout.

The earlier suspicion that retained cycles explain the async-tree slowdown was not supported by this probe. After the six-level I/O body, XLang3 had 15,259 tracked objects versus 15,241 before it; explicit collection removed 24 and left 15,219. CPython's tracked count fell from 25,111 to 24,147 during the body and explicit collection removed zero. These counts do not establish total memory use: this probe ran with `--no-memory` because XLang3's Windows psutil native module lacks `check_pid_range`, so RSS and private bytes were unavailable. The same psutil ABI gap is recorded separately and should not be confused with the async runtime cause.

## Callback-frame cache lifecycle follow-up — 2026-10-11

The fresh full pyperformance run on the fixed Release executable reached
`async_tree_io` at case 16 and `async_tree_io_tg` at case 17; both recorded
their 300-second timeouts before the runner advanced to case 18. This confirms
the I/O tree remains a full-suite failure on this build. Case 18,
`async_tree_memoization`, completed at 5.09 ± 0.38 seconds, and case 19,
`async_tree_memoization_tg`, completed at 5.13 ± 0.35 seconds; pyperf warned
that both measurements may be unstable. Case 20, `async_tree_none_tg`, completed
at 3.63 ± 0.29 seconds. Case 21, `asyncio_tcp`, completed at 3.92 ± 0.23
seconds; case 22, `asyncio_tcp_ssl`, hit its 300-second timeout. Case 23,
`asyncio_websockets`, completed at 478 ± 12 ms. Case 24, `base64`, also hit
the 300-second timeout. Its valid partial JSON retains 20 values each for
`base64_small`, `base64_large`, `urlsafe_base64_small`, `base32_small`,
`base32_large`, `base16_small`, `base16_large`, and `ascii85_small`; these
remain failed-definition evidence and are excluded from scored comparisons.
The preserved file and provenance are
[`base64-de2808c423b6e699.json`](data/pyperformance-xlang3-postfix-candidate-full-fast-20261011-partial/base64-de2808c423b6e699.json)
and its `.provenance.json` sidecar. At the latest check the runner had advanced
to case 25, `bpe_tokeniser`. The run log is
[`pyperformance-xlang3-postfix-candidate-full-fast-20261011.log`](data/pyperformance-xlang3-postfix-candidate-full-fast-20261011.log).

As screening evidence only, medians of the 20 retained raw values per subtest
were compared with the saved CPython 3.14.7 full-run values. XLang3 reached
0.079× on `urlsafe_base64_small`, 0.090× on `base16_small`, 0.108× on
`base64_small`, and 0.154–0.551× on the other five completed subtests. The
failed `base64` definition is still excluded from the official speed table and
aggregate; the XLang3 run is not paired in time with the CPython reference.
The per-subtest values are in this
[screening CSV](data/pyperformance-xlang3-base64-partial-vs-cpython3147-screening-20261011.csv).
The CPython values come from the saved
[3.14.7 full-run JSON](data/pyperformance-cpython3147-live-eval-full-fast-20261007.json).
The benchmark source's small cases make hundreds of tiny encode/decode calls
per iteration, while `base64_large` processes large buffers in fewer calls.
The much wider gap on the small cases therefore points toward XLang3's
per-call/VM overhead; this is an inference from workload shape, not proof
that native `binascii` byte loops are already optimal. `base32_large` also
remains 4.6× slower on a pure-Python encode/decode path, so optimization must
stay in generic VM/runtime execution rather than replacing `base64.py`.
The executable and runtime DLL hashes are respectively
`BE0E00D5875D5887B1C7B9345066B82E9DDA450C75FD62AFF8614823A1B7F97B` and
`E4C8ACE7413DC3782AC62513733EE664D3394BD942E666A126E9201711758249`.

The active executable was built at 01:11 on 2026-10-11, after the guarded
slot/double comparator source was last changed on 2026-10-10. Its full-suite
timeouts on both I/O tree variants therefore show that this narrow inliner does
not remove the dominant end-to-end async cost. The weak-identity frame-cache
reuse experiment was edited after this executable was built and is not included
in these measurements; it remains unbuilt and unmeasured.

Source tracing found a second general callback cost beside Python comparator
execution. `context_run_kw` delegates the callback to
`runtime_call_callable`; for an ordinary Python function that path constructs
an `Interpreter` and calls `run_function_value`. `Interpreter::run_function`
does reuse a thread-local frame vector and its backing buffers, but its pool
guard clears the retained frame's function and execution metadata when the
activation ends. The next native callback activation therefore resets the
frame's instruction-cache storage. The async-tree counter run records 65,322
slow and 55,992 fast `_contextvars.Context.run` calls. These counts and the
cache lifecycle identify a repeated callback-entry/cold-cache candidate; they
do not establish its share of elapsed time or prove that a persistent cache
would help.

Any follow-up should measure cache hit rates and activation costs on the
official callback workload before changing cache lifetime. A retained cache
must not keep callback functions, modules, closures, globals, or other Python
objects alive after their visible references disappear. It must also validate
current function/code identity, module/class mutation, and monitoring state
before reusing a specialization. Keep ordinary Python `asyncio` code
authoritative; this candidate belongs in generic VM/runtime execution or the
native `_contextvars` bridge, not a C++ reimplementation of the library.

An initial generic VM candidate is now prepared in the working tree. After
`clear_for_pop()` removes owning cache payloads, the outer frame pool records
only a weak module owner and function id. Reuse requires that exact live
module/function pair; an expired or different identity rebuilds the cache and
execution metadata. The regression fixture
[`native_callback_frame_cache_reuse.py`](../../tests/fixtures/core/native_callback_frame_cache_reuse.py)
exercises repeated `Context.run` calls plus property/method rebinding and
global rebinding and function `__code__` replacement. Correction: this source
was included in the fixed Release executable used by the subsequent
closure-inline run. The Release object for `xlang_vm_loop.cpp` was rebuilt
after the edited frame/cache headers, and rerunning the existing fixed-path
build command reported no pending work. The fixture output matches CPython
3.14.7. In a case-only `async_tree_eager_memoization_tg` comparison, the
pre-closure binary measured 5.61 s ±0.36 s and the current binary measured
5.69 s ±0.29 s; `pyperf compare_to` found no significant difference. This
comparison includes other changes between the two binaries and does not
isolate frame-cache reuse. It shows no measured gain yet, and the broader
async-tree slowdown remains unresolved.

## Evidence files

- [Timer comparison count, XLang3](data/async-tree-timer-compare-count-xlang3-io-l6-20261010.json)
- [Timer comparison count, CPython 3.14.7](data/async-tree-timer-compare-count-cpython3147-io-l6-20261010.json)
- [XLang3 object-retention probe](data/async-tree-gc-retention-xlang3-io-l6-20261010.json)
- [CPython object-retention probe](data/async-tree-gc-retention-cpython3147-io-l6-20261010.json)
- [Diagnostic scripts](../../benchmarks/diagnostics/async_tree_timer_compare_count.py) and [retention probe](../../benchmarks/diagnostics/async_tree_gc_retention_probe.py)

## Guarded runtime inliner trial

I added a generic IR-shape fast path in `runtime_call_callable` for the exact form `if isinstance(other, Class): return self.slot < other.slot; return NotImplemented`. It reads current code and globals each time and only commits when the method resolves canonical `isinstance`, the global class and both instance classes are identical, the builtin `type` check is unmodified, the live member descriptor addresses the same slot, both slot values are exact doubles, and tracing/profiling/monitoring are inactive. Everything else executes the original Python function frame. It does not implement or replace asyncio, `_heapq`, or any pure-Python library logic.

The 1.5-million-comparison microbenchmark median fell from 0.824 seconds before this fast path to 0.346 seconds after it (2.38× faster); CPython 3.14.7 took 0.0489 seconds, so XLang3 remains about 7.1× slower on this microbenchmark. The benchmark mutates the compared slots each iteration and is directional evidence, not a pyperformance score.

The exact pyperformance `async_tree_io` debug single-value run measured 8.94 seconds on XLang3 and 0.594 seconds on CPython 3.14.7 (0.0664× CPython/XLang3). The fast-mode case exceeded the 300-second harness cap, so this did not produce a valid fast-mode score. A direct uninstrumented six-level body measured 8.77–8.94 seconds on the candidate XLang3 build and 0.595–0.596 seconds on CPython. The targeted compare inliner helps the narrow comparator path but does not fix the overall async workload.

The wider async-tree `none` result in the preceding full report was 4.082 seconds versus 0.229 seconds (0.0562×). On the rebuilt current candidate, the matching debug single-value run measured 3.67 seconds versus 0.228 seconds (0.0621×); the debug mode is a screening result, not a stable score. Candidate `--perf-counters` evidence on the same official `none` workload reports 1,159,266 native calls, 1,188,684 `CallMethod` opcodes, 1,762,096 `LoadLocalAttr` opcodes, 534,039 BoundMethod allocations, 9,138,975 Instance increments, and 9,513,265 Instance decrements. Existing call-profile evidence on CPython 3.14.7 and XLang3 showed nearly equal Python call counts (1,148,050 and 1,157,378) for this body. The mismatch therefore remains predominantly the work XLang3 performs per call/attribute/ownership operation; it is not caused by Python 3.13, missed imports, or a shortage of function indices.

The BoundMethod count is a useful signal, but caching Task `_step`/`_wakeup` callbacks is already a rejected experiment: [the 2026-10-03 trial](asyncio-task-callback-cache-trial-20261003.md) found no repeatable async-tree gain and removed the cache. Do not repeat that change. The next investigation must measure a different per-call or attribute-dispatch cost and show an end-to-end gain before expanding the optimization.

The new `native_callback_slot_compare` fixture passed in the full XLang3 fixture suite and produced the same output on CPython 3.14.7. It exercises same-class ordering, non-instance `NotImplemented`, subclass fallback, descriptor fallback, and a shadowed `isinstance`. This verifies the principal semantic fallback cases, but it does not establish a full-suite performance win.

## Rich-comparison guard-cache experiment — source only

The current full run shows that the earlier direct slot/double inliner still
leaves both six-level I/O-tree variants over the 300-second limit. The
inliner re-analyzed the same 12 IR instructions and repeated globals/builtins,
class-slot, and descriptor checks on every `_heapq` comparison. The new source
experiment keeps those proofs in a lazily allocated cache attached to the
receiver class's existing rich-comparison cache. Function code, module
namespace, builtins namespace, and class versions guard the cached record;
all payloads are non-owning pointers and ordinary Python execution remains the
fallback after any mutation or observer activation.

The `native_callback_slot_compare` fixture now also exercises `__code__`
replacement and rebinding the class global after a successful comparison has
warmed the native callback cache. An isolated Release pair differing only in
the cached proof reduced a 1.5-million-comparison microbenchmark from 326.8 ms
to 270.8 ms (1.21x). The official debug `async_tree_io` workload moved from
8.89 s to 8.73 s (1.02x); the candidate still exceeded the 300-second fast-mode
cap. This is a real local comparison-path improvement, but it does not solve
the full async workload.

## Evidence files

- [Targeted comparison microbenchmark, XLang3](data/native-callback-slot-compare-xlang3-inline-20261010.json)
- [Targeted comparison microbenchmark, CPython](data/native-callback-slot-compare-cpython3147-20261010.json)
- [Official async_tree_io debug run, XLang3](data/pyperformance-async-tree-io-xlang3-inline-debug-20261010.json)
- [Official async_tree_io debug run, CPython](data/pyperformance-async-tree-io-cpython3147-debug-20261010.json)
- [Official async_tree_none debug run, XLang3](data/pyperformance-async-tree-none-xlang3-inline-debug-20261010.json) and [CPython](data/pyperformance-async-tree-none-cpython3147-debug-20261010.json)
- [Uninstrumented async tree body timings](data/async-tree-io-body-xlang3-inline-20261010.json) and [CPython](data/async-tree-io-body-cpython3147-20261010.json)
- [XLang3 async_tree_none counters](data/async-tree-none-inline-perf-counters-20261010.log)
- [Diagnostic scripts](../../benchmarks/diagnostics/native_callback_slot_compare_bench.py), [async-tree I/O timer counter](../../benchmarks/diagnostics/async_tree_timer_compare_count.py), [body timer](../../benchmarks/diagnostics/async_tree_io_body_timing.py), and [retention probe](../../benchmarks/diagnostics/async_tree_gc_retention_probe.py)

The comparator inliner is a validated narrow optimization, not completion of the requested performance goal. The overall 0.12834× score, remaining slow cases, async-tree timeouts, and 19 failed definitions remain unresolved.

### Full current-source Release run r4 — 2026-10-11

The complete current-source build attempted all 97 definitions: 61 completed
and 36 failed or timed out. Across 70 matched subtests, the CPython/XLang3
geometric mean is 0.14013x (XLang3 takes about 7.14x as long), with only two
nominal wins. `async_tree_none` is 4.99 s versus 229 ms on CPython (0.046x), and
the four async I/O variants timed out. The rich-comparison proof cache's 2%
debug-mode whole-case reduction is too small to change that outcome.

The full [r4 report and horizontal ratio chart](pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4-vs-cpython3147.md),
[all-97 status CSV](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4-vs-cpython3147-all-97-status.csv),
[matched subtests](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4-vs-cpython3147-subtests.csv),
and [raw runner log](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4.log)
are preserved. Against the previous r3 binary, r4's overall matched-set
geometric mean is 1.00x slower, while SciMark LU improved 3.35x (1.02 s to
303 ms) and `json_dumps` improved 1.20x (34.6 ms to 28.8 ms). These build-to-build
changes include many working-tree source edits and are not attributed to one
patch. The new LU result is still 4.03x slower than CPython. The dominant
execution-cost diagnosis remains unresolved: high Python call/attribute/ownership
cost and VM work per operation, beyond indexed local/name lookup.

An earlier fixed-path candidate used executable SHA-256
`E684D30F138B77C67B8D1A5BEEB636BBD42907D66D1C675FBF023ECDE26FB45F` and
runtime DLL SHA-256
`DC09E602E3CAC4A6335F2D52568D9B54870B7E389D18B3B50F8F26741BB2D33C`. The
fresh full-suite run above uses the separately recorded `BE0E…`/`E4C8…` hashes.
An exact pre-build copy of those binaries is preserved at
`build-repro/baseline-postfix-candidate-full-fast-20261011/Release/` for the
required fixed-baseline regression gate.

### Follow-up full-suite evidence — 2026-10-11 02:33 local

Case 25 `bpe_tokeniser` hit the 300-second timeout and the runner advanced to
case 26 `chameleon`. No BPE partial result was preserved. During the final
minute of the case, the XLang3 worker's CPU time stayed near 0.33 seconds while
its wall time reached five minutes. This suggests the worker was waiting or
stalled; it does not identify the blocking operation. The full run remains
active, and the executable/DLL hashes above were rechecked unchanged.

At 02:41 local, case 33 `dask` also hit the 300-second timeout. Its log shows
the scheduler failing to deserialize the task graph at cloudpickle's
`types.CodeType(...)` reduction with `TypeError: object.__init__ expected no
arguments`; the later `bytes.join` error and canceled futures followed that
deserialization failure. The pyperf worker had established local Dask TCP
connections, so this run does not point to a firewall block. Case 34
`deepcopy` completed at 2.52 ms, and the runner advanced to case 35
`deltablue`. The full log preserves the Dask traceback and worker output.

At 02:42 local the run had reached case 40 `fastapi`. Cases 35–36 completed;
cases 37 `docutils` and 38 `dulwich_log` ended as `Benchmark died`, not
timeouts. Case 39 `fannkuch` completed at 279 ms. These failures are recorded
separately from the Dask timeout, and the runner's full traceback is preserved
in the same log.

By 02:47 local, case 40 `fastapi` had timed out at 300 seconds. Case 41
`float` completed at 77.6 ms, and case 42 `gc_collect` was active. The timeout
is preserved as a failed definition; no FastAPI score is available.
Case 43 `gc_traversal` completed at 2.46 ± 0.33 ms (the run warned that its
standard deviation was 13% of the mean). By 02:50 local, case 48 `html5lib` was
active; case 47 `hexiom` completed at 46.8 ± 0.7 ms. Cases 44–46 completed:
`generators` at 325 ± 8 ms, `genshi_text` at 337 ± 49 ms, `genshi_xml` at 968
± 281 ms, and `go` at 916 ± 10 ms. The Genshi samples were unstable; the raw
full-suite log retains the warnings and values.

## Bounded full-suite rerun with explicit pyperf shim (2026-10-11)

The earlier shimmed runner depended on the caller's ambient `PYTHONPATH` to
include `benchmarks/diagnostics/pyperf_compat`. The current shell had an empty
`PYTHONPATH`, so an isolated XLang3 `mdp` run failed inside pyperf's Windows
priority setup with `AttributeError: psutil._psutil_windows has no attribute
proc_priority_set`. The runner now prepends its compatibility directory
explicitly whenever a shared dependency site is supplied. With that change,
the isolated `mdp` run passed worker setup and reached the workload, where it
exceeded a 30-second diagnostic cap. That check proves the startup hook now
runs; it does not establish why `mdp` is slow.

A corrected all-97 `--fast` run is underway with CPython 3.14.7, pyperformance
1.14.0, the same shared dependency site, and the fixed Release executable at
`build-repro/main-verify-20261006/Release/xlang3.exe`. The runner has a 300-
second per-definition cap and a 600-second `networkx*` override. At the first
documentation checkpoint it had reached case 13, `async_tree_eager_memoization`.
Cases 11 and 12, `async_tree_eager_io` and `async_tree_eager_io_tg`, each hit the
300-second cap and the runner advanced. Cases 5–10 completed, including
`async_tree` at 3.75 seconds and
`async_tree_cpu_io_mixed` at 4.69 seconds. These are fixed-binary baseline
measurements, not candidate improvements. The full-run log is
[`pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011.log`](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011.log);
its live run identity and fixed-binary hashes are recorded in
[`pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011.provenance.json`](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011.provenance.json).
the isolated `mdp` diagnostic is preserved alongside it.

The wrapper change fixes a missing environment assumption and the corrected
run demonstrates that a timed-out case no longer prevents later definitions
from being attempted. It does not yet explain the earlier seven-hour stall or
change runtime performance. The full-suite run and JSON comparison are still
incomplete.

The same run later reached case 21, `asyncio_tcp`. Cases 16 and
17, `async_tree_io` and `async_tree_io_tg`, each hit the 300-second cap and the
runner advanced. Case 18, `async_tree_memoization`, completed at 5.11 ± 0.14
seconds, and case 19, `async_tree_memoization_tg`, at 5.16 ± 0.21 seconds.
Case 20, `async_tree_tg`, completed at 4.46 ± 1.30 seconds (unstable).
This reconfirms the I/O-heavy async-tree variants as failures on the
fixed Release binary; the log remains the authoritative source for continuing
progress.

The first shimmed rerun was interrupted after the assistant misread the local
clock as elapsed hours. Its log records 21 completed definitions and the start
of case 22; it produced no complete JSON. The interruption is recorded in its
[provenance sidecar](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011.provenance.json).
A fresh all-97 rerun started at 03:52:40 local on 2026-10-11, using the same
fixed Release executable, matching preserved-baseline hashes, Python 3.14.7,
and 300-second per-definition cap. Its live log is
[`pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r2.log`](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r2.log),
and the command, dependency site, hashes, and reference-run identity are in
its [provenance sidecar](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r2.provenance.json).
At the first checkpoint it had completed cases 1–3 and started case 4,
`async_generators`; the full result remains pending.
At the 04:02 local checkpoint it had completed case 6 and entered case 7,
`async_tree_cpu_io_mixed_tg`. The fixed-binary `async_tree` result was 3.81 ±
0.18 seconds and `async_tree_cpu_io_mixed` was 4.80 ± 0.18 seconds; pyperf
flagged both fast-mode samples as unstable. These are run-progress values,
not a candidate-versus-baseline comparison. The updated checkpoint and results
are preserved in the r2 provenance sidecar.
At 04:06 local the rerun had completed 9 definitions and entered case 10,
`async_tree_eager_cpu_io_mixed_tg`. The eager async-tree results were 1.35 ±
0.09 seconds (`async_tree_eager`) and 2.68 ± 0.28 seconds
(`async_tree_eager_cpu_io_mixed`); pyperf marked both unstable. The r2 sidecar
now records the completed-case checkpoint and observed means for cases 1–9.
At 04:09 local, case 10 `async_tree_eager_cpu_io_mixed_tg` completed at
4.52 ± 0.18 seconds and the run entered case 11, `async_tree_eager_io`.
Case 11 reached the wrapper's 300-second limit and was reported as a timeout;
the runner advanced to case 12, `async_tree_eager_io_tg`. This independently
confirms the eager I/O-tree timeout on the fixed Release executable. The
timeout and current-case checkpoint are recorded in the r2 provenance sidecar.
Case 12 also reached its 300-second limit; at 04:18 local the runner advanced
to case 13, `async_tree_eager_memoization`. Both eager I/O variants now fail
under the same fixed Release run identity.
Case 13 completed at 3.36 ± 0.18 seconds (pyperf flagged the sample as
unstable); case 14, `async_tree_eager_memoization_tg`, is now running.
Case 14 completed at 5.46 ± 0.49 seconds (also flagged unstable), and case 15,
`async_tree_eager_tg`, has started.
Case 15 completed at 3.32 ± 0.08 seconds (flagged unstable), and the run has
entered case 16, `async_tree_io`, the non-eager I/O variant.
Case 16 also hit the 300-second cap, and the runner advanced to case 17,
`async_tree_io_tg`. Case 17 then hit the same cap at 04:35 local and advanced
to case 18, `async_tree_memoization`. All four all-I/O tree definitions timed
out on this fixed Release build; the runner continued past each one.
Case 18 completed at 5.67 ± 0.83 seconds, with pyperf marking the result
unstable, and the suite advanced to case 19, `async_tree_memoization_tg`.
Case 19 completed at 5.55 ± 1.06 seconds, also unstable; case 20,
`async_tree_tg`, has started.
Case 20 completed at 4.02 ± 1.08 seconds (unstable), and the run advanced to
case 21, `asyncio_tcp`.
Case 21 completed at 3.74 ± 0.13 seconds (flagged unstable); case 22,
`asyncio_tcp_ssl`, is now running.
Case 22 hit the 300-second cap. Case 23, `asyncio_websockets`, completed at
477 ± 9 ms, and case 24, `base64`, has started.
Case 24 hit its 300-second cap after producing eight valid subtest samples.
The runner preserved them in
[`base64-a5204992ba385d96.json`](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r2-partial/base64-a5204992ba385d96.json)
and advanced to case 25, `bpe_tokeniser`. These partial values remain separate
from the scored full-definition results.
Case 25 hit the 300-second cap with no retained partial result; the runner
advanced to case 26, `chameleon`.
Case 26 completed at 135 ± 2 ms (flagged unstable) and case 27,
`chaos`, has started.
Cases 27–29 completed: `chaos` at 452 ± 7 ms, `comprehensions` at 174 ±
20 µs, and both `concurrent_imap` subtests (`bench_mp_pool` at 321 ± 9 ms,
`bench_thread_pool` at 9.19 ± 0.56 ms). The measurements were flagged
unstable, and case 30, `coroutines`, is active.
Case 30 completed at 141 ± 2 ms and case 31, `coverage`, at 702 ± 35 ms;
both were flagged unstable. Case 32, `crypto_pyaes`, is active.
Case 32 completed at 339 ± 4 ms (flagged unstable), and case 33, `dask`, has
started.
Case 33 hit the 300-second cap. Its log shows Dask failing to deserialize a
cloudpickled task graph with `TypeError: object.__init__ expected no
arguments`, followed by cancelled futures; this is a compatibility failure,
not a scored runtime result. The suite continued to case 34: `deepcopy` at
2.78 ± 0.42 ms, `deepcopy_reduce` at 27.6 ± 1.1 µs, and `deepcopy_memo` at
301 ± 40 µs. Cases 35–36 completed `deltablue` at 39.9 ± 0.6 ms and
`django_template` at 516 ± 35 ms. Cases 37 `docutils` and 38 `dulwich_log`
both died; case 39 `fannkuch` completed at 285 ± 3 ms. These fast-mode samples
were flagged unstable. The run has advanced to case 40, `fastapi`.
Case 40 hit the 300-second cap. Case 41, `float`, completed at 86.8 ± 7.5 ms
(unstable), and the runner advanced to case 42, `gc_collect`.
Case 42's `create_gc_cycles` subtest completed at 2.26 ± 0.27 ms; case 43,
`gc_traversal`, completed at 2.20 ± 0.20 ms. Both were unstable, and case 44,
`generators`, has started.
At the 12:18 UTC checkpoint, the r2 run had completed cases 46–49 and entered
case 50, `json_loads`. The new results were `go` 932 ± 32 ms, `hexiom` 47.4 ±
1.4 ms, `html5lib` 653 ± 27 ms, and `json_dumps` 31.7 ± 1.5 ms; pyperf marked
each result unstable. These values are fixed-binary baseline measurements,
not candidate wins, and the JSON comparison remains pending. The provenance
sidecar now records progress through `json_dumps` and the current `json_loads`
case.
`json_loads` completed at 79.7 ± 1.4 µs, and the three `logging` subtests
completed at 93.8 ± 2.7 µs (`logging_format`), 1.12 ± 0.01 µs
(`logging_silent`), and 87.6 ± 9.2 µs (`logging_simple`). These fast-mode
samples were flagged unstable. Case 52 `mako` died before producing a timing:
the pure-Python Mako template compiler raised `TypeError: ** argument must be
dict` while building `util.SetLikeDict`. This is a compatibility failure, not
a performance result. Case 53 `mdp` is now running; the provenance sidecar
records that checkpoint and the Mako failure.
Source tracing localizes this failure to XLang3 call expansion: both the
ordinary `CALL_EX` path at `xlang_vm_ops_call.h:516` and the native-call
materializer at `:815` accept only `value_as_dict()`. Mako's
`SetLikeDict` is a Python subclass of `dict`, so XLang rejects it before the
callee runs. The runtime already has `mapping_is_dict()` and
`dict_storage_from_value()` support for dict subclasses (`mapping.cpp:927`),
but these call-expansion paths do not use that support. The fix must preserve
the exact-dict fast path while routing subclasses through semantics that honor
their mapping overrides; changing Mako or its Python source would mask the
interpreter bug.
Case 53 `mdp` exceeded the configured 300-second cap and the runner advanced
to case 54, `meteor_contest`; the timeout is recorded separately from scored
results in the provenance sidecar.
Cases 54–55 then completed: `meteor_contest` at 1.18 ± 0.02 seconds and
`nbody` at 304 ± 13 ms (both unstable fast-mode samples). Case 56,
`networkx`, is running with its configured 600-second timeout override; the
sidecar records this checkpoint.
Against the saved CPython 3.14.7 reference (`meteor_contest` 89.46 ms,
`nbody` 82.85 ms), those XLang3 means are approximately 0.076× and 0.273×
CPython/XLang3 respectively (about 13.2× and 3.7× longer). These are
unpaired, unstable fast-mode comparisons and should be treated as directional.
NetworkX `shortest_path` completed at 1.06 ± 0.07 seconds, versus 425.77 ms
in the saved CPython reference: approximately 0.40× CPython/XLang3 (2.5×
longer). This sample is also unstable. The runner advanced to case 57,
`networkx_connected_components`, and the sidecar records the result.
`networkx_connected_components` completed at 851 ± 83 ms, versus 380.93 ms
for CPython (about 0.45× CPython/XLang3, or 2.2× longer); this fast-mode
sample was flagged unstable. Case 58, `networkx_k_core`, exceeded its
600-second cap; the runner advanced to case 59, `nqueens`. The k-core timeout
is recorded separately from timings in the provenance sidecar.
`nqueens` completed at 952 ± 313 ms; pyperf reported a 33% standard deviation
and a maximum nearly twice the mean. Against the saved CPython reference of
76.88 ms, the nominal ratio is 0.081× CPython/XLang3 (about 12.4× longer),
but the highly unstable sample makes that ratio especially tentative. Case
60, `pathlib`, is now running.
`pathlib` then completed at 582 ± 32 ms and `pickle` at 17.9 ± 0.2 µs (both
flagged unstable). Against the saved CPython results, their nominal ratios are
about 0.073× and 0.53× CPython/XLang3 respectively. The first is roughly
13.8× slower; the second about 1.9× slower. Case 62, `pickle_dict`, has
started.
`pickle_dict` measured 24.3 ± 0.7 µs versus 23.59 µs for CPython (0.97×),
and `pickle_list` measured 4.01 ± 0.07 µs versus 4.02 µs (about 1.00×).
In contrast, the pure-Python pickle case measured 3.67 ± 0.05 ms versus
257.5 µs (0.070×, roughly 14.3× longer). All three XLang3 samples were
flagged unstable. This native/simple-versus-pure-Python gap points toward
Python-level VM/runtime execution costs rather than a blanket penalty in the
pickle module; it is a root-cause lead, not proof of which VM operation
dominates. The benchmark source explicitly sets `sys.modules['_pickle'] = None`
for the pure-Python case, forcing the same `pickle.py` implementation to run
without CPython's native accelerator. Thus the roughly 14× gap is evidence
about executing pure Python on XLang3, not justification for replacing
`pickle.py` with C++. `pidigits` measured 274 ± 118 ms versus 163.0 ms
(nominal 0.59×),
with 43% standard deviation and a maximum more than twice the mean, so that
ratio is unreliable. The runner has reached case 66, `pprint`.
`pprint` exceeded its 300-second cap; the runner continued to case 67,
`pyflate`, and the timeout is recorded in the provenance sidecar.
`pyflate` completed at 2.77 ± 0.28 seconds (unstable), versus 344.9 ms for
CPython 3.14.7: about 0.125× CPython/XLang3 or 8× longer. Case 68,
`python_startup`, is now running.
`python_startup` completed at 33.8 ± 1.2 ms versus 18.60 ms for CPython
(0.55×), and `python_startup_no_site` at 21.0 ± 0.8 ms versus 15.75 ms
(0.75×); both are unstable fast-mode samples. Case 70, `raytrace`, is now
running, with the sidecar preserving these results.
`raytrace` completed at 2.24 ± 0.25 seconds versus 227.3 ms for CPython
(about 0.10× CPython/XLang3, roughly 9.9× longer; unstable). The run advanced
to case 71, `regex_compile`.
`regex_compile` then measured 1.29 ± 0.07 seconds versus 98.1 ms for CPython
(0.076×, about 13.2× longer), and `regex_dna` measured 215 ± 4 ms versus
137.4 ms (0.64×, about 1.6× longer); both fast-mode results were flagged
unstable. Case 73, `regex_effbot`, is running.
`regex_effbot` completed at 12.7 ± 0.2 ms versus 1.86 ms for CPython
(0.15×), and `regex_v8` at 65.1 ± 1.6 ms versus 15.7 ms (0.24×).
`richards` measured 347 ± 7 ms versus 34.1 ms (0.098×), and
`richards_super` 414 ± 9 ms versus 38.4 ms (0.093×). Those four results were
all flagged unstable; the roughly 10× Richards gaps are consistent with the
large pure-Python execution penalty seen in the forced-Python pickle case.
The runner has reached case 77, `scimark`.
The five SciMark kernels all completed without pyperf instability warnings:
`fft` 960 ms (0.24× CPython), `lu` 1.09 s (0.069×), `monte_carlo` 543 ms
(0.097×), `sor` 854 ms (0.114×), and `sparse_mat_mult` 17.4 ms (0.18×).
`spectral_norm` completed at 517 ms (0.147× CPython), also without an
instability warning. The benchmark source implements these numerical loops
in Python over the native `array('d')` container. This is stronger evidence
that tight Python loops, indexing, and numeric operations need VM/IR/runtime
optimization; translating the benchmark or its library code into C++ would
violate the user's pure-Python-library rule. The runner advanced to
`sphinx`, which failed during Babel's Windows timezone initialization:
XLang3's native `winreg.QueryInfoKey()` rejected the key argument. The full
traceback and failure classification are preserved in the log and sidecar.
`sqlalchemy_declarative` completed at 1.33 ± 0.03 seconds versus 89.23 ms for
CPython (about 0.067× speed); pyperf flagged the XLang3 sample unstable. The
runner advanced to case 81, `sqlalchemy_imperative`.
`sqlalchemy_imperative` completed at 227 ± 5 ms versus 11.24 ms for CPython
(0.050×, about 20× longer; unstable). Case 82, `sqlglot_v2`, is running.
`sqlglot_v2_normalize` measured 1.48 ± 0.03 seconds versus 90.4 ms for
CPython (0.061×), and `sqlglot_v2_optimize` 677 ± 21 ms versus 42.5 ms
(0.063×); both were unstable. These pure-Python SQLGlot workloads show the
same large execution gap as pure-Python pickle, consistent with a broad VM
and runtime cost. Case 84, `sqlglot_v2_parse`, is now running.
`sqlglot_v2_parse` completed at 18.0 ± 0.5 ms versus 0.988 ms for CPython
(0.055×, about 18× longer), and `sqlglot_v2_transpile` at 21.9 ± 0.8 ms
versus 1.23 ms (0.056×, about 18× longer). `sqlite_synth` measured 6.10 ±
0.10 µs versus 1.83 µs (0.30×), and `telco` 41.3 ± 1.2 ms versus 5.71 ms
(0.14×). All four were flagged unstable. Case 87 `sympy` failed importing
`sympy.polys.polyutils`: XLang's parser rejected valid source at line 51,
column 14 with `invalid syntax`; this compatibility failure is detailed in
the worker log and provenance. The run has reached case 89, `tomli_loads`.
Case 89, `tomli_loads`, exceeded its 300-second cap; the runner advanced to
case 90, `tornado_http`, and the timeout is recorded in the provenance.
`tornado_http` completed at 1.46 ± 0.06 seconds versus 140 ms for CPython
(0.096×, about 10× slower; unstable). The runner advanced to case 91,
`typing_runtime_protocols`.
`typing_runtime_protocols` measured 2.83 ± 0.09 ms versus 128.1 µs for CPython
(0.045×, about 22× slower). `unpack_sequence` measured 158 ± 3 ns versus
36.6 ns (0.23×), `unpickle` 64.0 ± 2.0 µs versus 9.83 µs (0.15×), and
`unpickle_list` 11.3 ± 1.5 µs versus 3.22 µs (0.29×); all were flagged
unstable. The earlier near-parity result applies to `pickle_dict` and
`pickle_list` dumps, not to the native unpickling path. Case 95,
`unpickle_pure_python`, is running.
`unpickle_pure_python` completed at 2.24 ± 0.03 ms versus 168.3 µs for
CPython (0.075×, about 13.3× slower; unstable). Case 96 `xdsl` failed during
import because its dialect accessed `.get` on `GenericAlias`; the failure is
recorded as a compatibility issue, not a timing. The full run has reached its
last definition, case 97 `xml_etree`.
The 97-definition fixed-Release run is now terminal: 77 definitions completed,
20 failed or timed out (6 worker deaths, 14 timeouts), and 87 subtests matched
the saved CPython 3.14.7 run. XLang3 was nominally faster on two; the matched
geometric mean was 0.12922× CPython/XLang3. See the complete
[comparison and horizontal ratio chart](pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r2-vs-cpython3147.md),
[all-97 status list](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r2-vs-cpython3147-all-97-status.csv),
and [matched-subtest data](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r2-vs-cpython3147-subtests.csv).
This is a baseline run of the fixed Release executable, not a candidate
speedup. The complete JSON, log, and hashes are preserved with that report.

### Full fixed-Release rerun r5 — 2026-10-11

The next fixed-path full run again attempted all 97 definitions and finished with
61 completed, 36 failed or timed out, and 70 CPython-matched subtests. Its
geometric-mean CPython/XLang3 ratio is **0.14125x** (about 7.08x slower), with
one nominal win. The r4 and r5 runs compared on the same 70 subtests show only
a **1.0080x** geometric-mean XLang3 speedup; this is sequential `--fast` data,
not paired statistical evidence. It does not establish a meaningful broad
gain. `async_tree_none` improved 1.162x (4.986 s to 4.291 s), `raytrace` moved
from 2.34 s to 2.08 s, and `asyncio_tcp` improved 1.282x (5.069 s to 3.953 s),
while several cases regressed. The full per-case comparison and provenance are
in [r4 vs r5](data/pyperformance-xlang3-fixed-release-r4-vs-r5-20261011.md).
The [r5 CPython comparison](pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r5-vs-cpython3147.md),
[all-97 status table](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r5-vs-cpython3147-all-97-status.csv),
[matched timings](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r5-vs-cpython3147-subtests.csv),
and [raw results and log](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r5.pyperf.json)
are preserved.

Of the 36 failures, 9 timed out: four async I/O workloads plus `base64`,
`bpe_tokeniser`, `mdp`, `pprint`, and `xml_etree`. Most of the 27 worker deaths
are missing optional benchmark packages in the shared dependency site; the
status CSV has the exact package/error for each. `asyncio_tcp_ssl` instead
reports a runtime `TypeError`, so it is a compatibility defect to investigate,
not a dependency omission. Failure counts are unchanged from r4; the 300-second
cap still prevents a score for the four async I/O cases.

This run evaluated a source set with both VM/IR and generic-runtime changes, so
the r4/r5 differences cannot be attributed to one hot-path mechanism. The
remaining evidence still points to costs paid for Python-level object and call
traffic: the older `async_tree_none` diagnostic counted about 1.16 million
native calls, 534 thousand temporary bound methods, and 9.5 million instance
value decrefs. Those counts identify high-volume work, not its time share. The
near-flat 70-case geomean says recent narrow gains have not removed the general
interpreter overhead. The next optimization needs a profile of a matched
slow workload that separates dispatch, generic calls, attribute lookup, and
value ownership before another specialization is added.

An r5 `--perf-counters` shape run now executes the same `NoneAsyncTree.run()`
from pyperformance's unchanged source on the fixed Release executable. It
counted **17,377,951 VM dispatches**: 1.66M `LoadLocalAttr`, 1.63M
`LoadLocalPair`, 1.39M `JumpIfFalse`, 1.20M `CallMethod`, 1.05M `StoreLocal`,
and 0.94M `StoreAttr`. It also counted 1.18M native calls and 9.69M instance
value decrefs. These are counts, not a timed attribution; the profile includes
module setup and should guide a timing profile, not be treated as proof that a
specific operation consumes the corresponding share of the 4.29-second
benchmark. The complete output is
[`async-tree-none-perf-counters-r5-20261011.log`](data/async-tree-none-perf-counters-r5-20261011.log).

The CPython comparison also points at VM throughput rather than a missing
Python implementation: CPython 3.14.7's native Task step uses `PyIter_Send`
for ordinary coroutine resumes, and XLang3's current native Task already calls
`generator_send` directly for the same case. That removes repeated `.send`
method lookup on both sides, yet the whole `async_tree_none` workload remains
18.7x slower. The next experiment therefore must target the remaining
per-operation VM/value cost or task/event-loop scheduling, with an official
case A/B and the fixed regression gate; adding another `.send` shortcut would
repeat an optimization already present. See CPython's
[3.14.7 `_asynciomodule.c`](https://github.com/python/cpython/blob/v3.14.7/Modules/_asynciomodule.c#L2913-L2933)
and XLang3's [`step_impl`](../../src/runtime/modules/system/asyncio_task.cpp).
