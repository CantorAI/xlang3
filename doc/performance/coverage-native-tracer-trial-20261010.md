# Own native Coverage tracer trial (2026-10-10)

Status: integrated and built, not accepted or performance validated. Parent is
`5ccf64b1c6d77c48852b9fecd6ad36f8019ac9fe`; its accepted Release remains preserved
under `build-repro/controls/numeric-frame-code-cache-accepted-20261010`.
The fixed baseline remains `build-repro/Release`, and the candidate run path is
`build-repro/main-verify-20261006/Release/xlang3.exe`.

## Why this addresses the full benchmark

The installed Coverage 7.3.2 package selects its native `coverage.tracer.CTracer`
on CPython 3.14.7, whereas the previous XLang3 build selected Python `PyTracer`.
The previous frame-code cache eliminated about 80% of Code allocations in the
Coverage diagnostic, but the combined checkpoint improved original Coverage
elapsed time only 1.015197x against the preceding accepted XLang3 build. This
suggested examining the remaining Python tracer execution, rather than claiming
that the allocation reduction represented an equivalent elapsed-time gain.

This trial supplies XLang3's own `coverage.tracer` module with `CTracer` and
`CFileDisposition`. It neither loads the CPython extension nor rewrites the
pure-Python Coverage package. Collector, file selection, plugin policy, context
callbacks, and warning behavior continue to execute their original Python code.
The installed CPython native module is the behavioral reference.

## Design boundaries

- Native state owns its Python `Value` fields and reports their physical edges
  through the existing dynamic native GC traversal. Stack aliases are not counted
  as additional physical owners.
- Field replacement publishes the incoming owner before retiring the old one;
  callbacks copy the callable before entering Python, allowing replacement and
  finalizer reentry without borrowing a retired value.
- Native hook installation has a thread-local delivery mode. VM delivery calls
  the direct native entry point, while explicit callable invocation retains its
  separate reinstallation behavior. Hook identity remains visible to `sys.gettrace`.
- Generator resume and suspension flags preserve packed arc semantics. Native
  exception unwind supplies balanced return events.
- The initial bridge still materializes visible frames and locals. It also adds
  a relaxed native-capability read to instruction dispatch; its cost must pass
  the unchanged full regression gate before acceptance.

## Actual checks so far

The exact existing VS environment/Ninja command built the candidate successfully.
All 55 actual CTest checks passed, including the core and compatibility fixtures.
The original unmodified `bm_coverage` workload completed one iteration with
`HAS_CTRACER` true and Fibonacci(25) equal to 75025. This was a smoke check, not a
scored measurement.

The scored original `bm_coverage` workload now has a valid 20-value XLang3
control/candidate comparison. The accepted XLang3 build had a 12.276435 s median;
the native-tracer candidate had a 1.409158 s median, an 8.7119x improvement.
Both phases had zero process-monitor errors and zero overlapping build/test
processes. A separate fresh CPython 3.14.7 run had a 62.136 ms median (62.380 ms
mean, 1.3 ms standard deviation). The native-tracer candidate therefore remains
22.6788x slower than CPython on this workload; the accepted XLang3 build was
197.5751x slower. CPython's `pyperf` output warns that this fast run is not stable
to a 1% threshold, so these ratios are useful point estimates, not precision
claims. The XLang3 phases are also sequential and unpaired.

The first original-workload attempt is retained but invalid: its XLang3 control
returned 20 values, while the one-second PowerShell/CIM activity watcher timed
out three times. A fresh controller uses in-process `psutil` 7.2.2 snapshots,
preserving the same one-second poll, process identity fields, build/runtime
classification, fail-closed scanner-error rule, and no-worker-exception policy.
Its warm scans over roughly 422 processes took about 30 ms and the final XLang3
comparison completed with no scanner errors. The invalid attempt is not included
in any score.

The portable eight-group contract passed 8/8 on fixed CPython 3.14.7 and 7/8 on
the first XLang3 draft. The failed group demonstrates a real VM issue: an ordinary
tracer policy callback's exact exception remains pending, but call-event failure
returns the interpreter result before the caller's `try/except` can handle it.
That failure is retained and must be corrected before acceptance.

The incremental R3 correction queues the exact native callback exception into
the existing deferred VM exception slot and preserves its traceback. Leaving
the emitter clears the trace recursion guard before normal handler dispatch.
The rebuilt candidate passed all eight portable groups and all six focused
groups. The first failed receipt remains unchanged; the passing R3 receipts
authenticate a different rebuilt runtime DLL.

The subsequent context-callback tests exposed a second real error: the public
`generator.close()` wrapper replaced a pending Python exception with RuntimeError.
The generic runtime correction preserves the owning interpreter exception across
VM-state retirement and synthesizes RuntimeError only when no exception is pending.
On the final R5 build, all eight portable groups, six broader groups, and four
context-error/traceback groups passed. Independent normal-close, finally-error,
and ignored-GeneratorExit controls passed on CPython and XLang3. The logical
concurrency test also passed on both runtimes. A standalone native-tracer fixture
is now registered in both core fixture runners; the final all55 CTest run and
separate Python fixture runner completed successfully, including both SQLite API
checks. Durable performance boundary comments are in the native module and hook.

Six further groups passed on both runtimes: exact packed arcs, generator resume,
throw/close/unwind, context attribution, callback field mutation, and rooted
native-field cycles followed by explicit breaking. These do not establish complete
plugin/concurrency/ABI parity. In particular, unreachable native-cycle collection
is not asserted as a shared contract because the referenced CPython native base
types do not expose GC traversal.

Raw receipts are in `doc/performance/data`:

- `coverage-native-tracer-portable-cp-r1-20261010.json`
- `coverage-native-tracer-portable-x-r1-20261010.json`
- `coverage-native-tracer-focused-cp-r1-20261010.json`
- `coverage-native-tracer-focused-x-r1-20261010.json`
- `coverage-native-tracer-portable-x-r3-20261010.json`
- `coverage-native-tracer-focused-x-r3-20261010.json`
- `coverage-native-tracer-original-smoke-r2-20261010.json`
- `coverage-native-tracer-portable-x-r5-20261010.json`
- `coverage-native-tracer-focused-x-r5-20261010.json`
- `coverage-native-tracer-context-errors-cp-r1-20261010.json`
- `coverage-native-tracer-context-errors-x-r5-20261010.json`
- `coverage-native-tracer-weak-concurrency-cpython-r4-20261010.json`
- `coverage-native-tracer-weak-concurrency-xlang3-r4-20261010.json`

Build/application receipts and the complete CTest log are retained under
`scratch/performance/coverage-native-tracer-r2-*`; they must be published with
the final checkpoint rather than treated as committed benchmark evidence now.

## Remaining acceptance work

The first final gate attempt refused foreign build activity before launching a
benchmark child. After those processes were confirmed absent, the complete R5b
gate ran with valid observations, zero recorded overlaps, completed cleanup, and
unchanged hashes. Ten cases passed; GC traversal remained inconclusive after the
gate's built-in confirmation. This is exit 2, not a pass or accepted checkpoint.

| GC attempt | Candidate / fixed baseline elapsed | 95% ratio interval |
| --- | ---: | --- |
| First | 1.107992x | [1.094577, 1.133640] |
| Confirmation | 1.074358x | [1.067331, 1.112820] |

The confidence interval crosses the unchanged 1.10 acceptance limit. Preserve
`coverage-native-tracer-fixed-gate-r5-20261010.json` and
`coverage-native-tracer-fixed-gate-r5b-20261010.json`, including both attempts and
all raw arrays. Do not retry merely to find a passing sample. Investigate the
observed GC cost and measure the original affected benchmark before acceptance.
An unscored startup object diagnostic counted 2292 visible tracked objects in the
accepted control and 2317 in the native candidate: 18 additional properties,
3 types, 2 static methods, 1 module, and 1 module spec. These counts identify a
possible registration/graph difference; they do not establish its CPU cost.

Fresh all-97 CPython 3.14.7 and validated-XLang3 captures must preserve all
124 subtest rows and failures. No fresh CPython-relative speed or full-suite win
has been established by this draft.

## R6: lazy locals on native trace delivery

The remaining `bm_coverage` gap points to the VM-to-tracer bridge, not IR
arithmetic or ordinary interpreter dispatch. On every trace event, the bridge
was allocating a Python locals dictionary, string keys, and a new set of
entries. Native Coverage line handling normally reads only `f_lineno`. R6 now
keeps an owning value snapshot for retained-frame correctness and defers the
dictionary until `f_locals` is requested. The first scope is native delivery on
non-entry, non-generator physical functions without cells, free variables, or
logical-frame projections. Manual tracing and complex frames retain eager
locals. Once a callback materializes `f_locals`, later events refresh that same
mapping eagerly. Publication swaps the complete snapshot before retiring old
owners, so finalizers cannot observe a partially refreshed frame.

The retained-frame fixture checks values and deleted locals after function
return. R6 passed the full 55-test CTest suite, the eight native portable
contract groups, and six focused tracer groups. A new direct `bm_coverage`
capture returned a 1.51 second mean with 29% relative standard deviation and a
2.80 second maximum. It is too noisy to accept or compare; it is retained as a
diagnostic only. The fixed 11-case acceptance gate and a process-watched
original-workload comparison have not yet completed for R6. Do not call this an
R6 speedup or a CPython win.

The valid R5 original-workload comparison remains the latest stable result:
native Coverage reduced XLang3's median from 12.276 seconds to 1.409 seconds,
an 8.71x gain over the accepted XLang3 parent. The separate CPython 3.14.7
median was 62.1 ms, so R5 remained about 22.7x slower. Those sequential
measurements are point estimates, and the CPython pyperf run warned about
stability. R6 has not superseded them.
