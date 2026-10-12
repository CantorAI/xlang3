# Candidate hot-path triage — 2026-10-11

## Result

The current Release candidate passes correctness checks but does **not** materially reduce the representative pyperformance gaps against CPython 3.14.7. The measured changes are near noise relative to the preserved XLang3 baseline, while several pure-Python workloads remain roughly 10–19× slower. Treat this as diagnosis evidence, not a speedup claim.

| Official pyperformance case | CPython 3.14.7 | XLang3 fixed baseline | Current candidate | CPython / candidate | Candidate / baseline |
|---|---:|---:|---:|---:|---:|
| `json_dumps` | 7.64 ms | 31.73 ms | 31.50 ms | 0.242× | 0.993× |
| `pickle` | 9.43 µs | 17.89 µs | 18.27 µs | 0.516× | 1.021× |
| `richards` | 34.14 ms | 346.89 ms | 336.95 ms | 0.101× | 0.971× |
| `scimark_fft` | 227.37 ms | 959.74 ms | 982.91 ms | 0.231× | 1.024× |
| `scimark_lu` | 75.29 ms | 1.091 s | 1.122 s | 0.067× | 1.028× |
| `scimark_monte_carlo` | 52.83 ms | 542.83 ms | 561.78 ms | 0.094× | 1.035× |
| `scimark_sor` | 97.53 ms | 854.44 ms | 861.79 ms | 0.113× | 1.009× |
| `scimark_sparse_mat_mult` | 3.14 ms | 17.44 ms | 18.75 ms | 0.168× | 1.075× |
| `sqlglot_v2_parse` | 0.988 ms | 18.03 ms | 18.37 ms | 0.054× | 1.019× |

`CPython / candidate` is the speed ratio: values below 1× mean XLang3 is slower. The fast-mode runs are directional; pyperf flagged several cases as unstable. The candidate/baseline differences are not evidence of a robust regression or improvement. In particular, a 3% Richards change does not alter its approximately 9.9× gap, and the Python-loop SciMark cases show no candidate gain.

## What this says about the bottleneck

The poor SciMark ratios occur in Python loops over numeric arrays, while Richards and SQLGlot exercise Python calls, object operations, and dispatch. Their common weakness is consistent with broad VM/runtime per-operation overhead; it is not evidence that one pure-Python standard-library implementation needs to be rewritten in C++. The native `json`/`pickle` cases remain separate module and object-conversion paths and should be profiled independently.

The current dirty source contains changes across VM operations, runtime/value/object behavior, builtins, and IR lowering. Passing the regression check establishes only that its 11 microcases remain within the configured 10% limit; it does not validate an overall pyperformance gain. No performance optimization is accepted by this measurement.

## VM operation counts

The current candidate's existing `--perf-counters` instrumentation was used on one invocation of the official SciMark LU body (`bench_LU(1, 100)`). The top VM counts were `GetItem` 3,046,133; `LoadLocal` 2,072,944; `LoadLocalAttr` 1,091,946; `Call` 1,045,343; `GuardedLocalListGetItem` 1,039,233; and `SetItem` 367,244. This diagnostic invocation imports the benchmark through `runpy`, so startup/import activity is included and counts must not be treated as exact body-only totals or timing attribution. The raw log and decoder output are [`scimark-lu-opcounts-current-release-20261011.log`](data/scimark-lu-opcounts-current-release-20261011.log).

The simpler call-loop diagnostic is cleaner: the IR for `identity(value)` is a direct `ReturnLocal`, but the loop executes a `LoadModuleSlot` and a `Call` for each invocation. Its current XLang3 time is 3.72 ms versus 1.16 ms on CPython for 50,000 calls (3.21× elapsed); the integer-only control is 1.18 ms versus 0.894 ms (1.32×). This narrows the residual gap to caller/call-site execution after the trivial function body is recognized; indexed module-slot lookup alone does not make calls CPython-speed. The source IR is [`vm-hotpath-microbench-ir-20261011.txt`](data/vm-hotpath-microbench-ir-20261011.txt), and the counts are [`vm-hotpath-microbench-current-opcounts-20261011.log`](data/vm-hotpath-microbench-current-opcounts-20261011.log).

## Validation and artifacts

- Current candidate executable: `build-repro/main-verify-20261006/Release/xlang3.exe`, SHA-256 `E2CE95BBBBDFA68EFA8EDF76E9C73FC98F448B288A586CC044A276F6C97BD1D5`.
- Preserved fixed baseline executable: `build-repro/baseline-postfix-candidate-full-fast-20261011/Release/xlang3.exe`, SHA-256 `BE0E00D5875D5887B1C7B9345066B82E9DDA450C75FD62AFF8614823A1B7F97B`.
- Candidate representative pyperf JSON: [`candidate-representative-fast-20261011.json`](data/candidate-representative-fast-20261011.json) and [`candidate-representative-fast-correct-names-20261011.json`](data/candidate-representative-fast-correct-names-20261011.json).
- CPython 3.14.7 reference JSON: [`pyperformance-cpython3147-live-eval-full-fast-20261007.json`](data/pyperformance-cpython3147-live-eval-full-fast-20261007.json).
- Fixed XLang3 baseline full-suite JSON: [`pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r2.json`](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r2.json).
- Correctness: Python 3.14.7 fixture runner passed; CTest passed 55/55.
- 11-case fixed-baseline regression gate passed, with output at [`candidate-vs-postfix-baseline-11cases-20261011.json`](data/candidate-vs-postfix-baseline-11cases-20261011.json).
- A later source-matching rebuild also passed 55/55 CTest tests and the 11-case fixed-baseline gate; its report is [`candidate-restored-11cases-20261011.json`](data/candidate-restored-11cases-20261011.json). The executable hash matches the candidate listed above.
- This candidate has not yet completed a full 97-case comparison. The preserved full-suite baseline completed 77/97 cases and measured a 0.129× geometric mean versus CPython 3.14.7.

## Rejected exact-string lookup trial

A trial routed exact-string `GetItem` through `mapping_get_string_item`. It passed the fixture suite but measured 41.9 ms on `telco`, versus 41.3 ms for the fixed baseline, and showed no meaningful change in the local dict-loop control. The helper recomputes a hash from a `string_view`; the existing generic path keeps the `StringObject` and uses its cached hash. The runtime fast path was removed after this result. The cross-type-equality fixture remains because it covers an important semantic constraint for future lookup specialization; it does not imply the rejected fast path is present.

An earlier branch-ordering trial moved the cached trivial-function return ahead of other `CALL` cache cases. Its focused `function_calls` ratio was 0.976× against the fixed baseline, with a 95% interval of 0.938–1.003×; the preceding candidate measured 0.981× with interval 0.976–1.001×. These intervals overlap parity, so the apparent difference is not a demonstrated gain. The branch-order change was removed. Its raw check is [`callsite-trivial-fastpath-20261011.json`](data/callsite-trivial-fastpath-20261011.json).

## Canonical `isinstance(index, tuple)` call-site specialization

`ArrayList.__getitem__` in SciMark repeatedly calls `isinstance(idx, tuple)` for matrix indexing. The generic builtin implementation spends time resolving the value's runtime class and running the full class-info path on every call. The VM now caches a narrowly guarded specialization for the canonical builtin `isinstance` and canonical builtin `tuple`: exact tuple values return true; tagged integer indexes return false. It falls back to the normal native builtin for every other argument shape/value, and whenever class versions, `type.__instancecheck__`, tracing, profiling, debugging, or monitoring can change observable behavior. The scalar cache stores only borrowed canonical builtin class pointers and version numbers; it owns no Python values and is retained across short frame reuse.

The semantic fixture [`isinstance_builtin_mro_fastpath.py`](../../tests/fixtures/core/isinstance_builtin_mro_fastpath.py) covers positive and negative results, builtin-function rebinding, patched `type.__instancecheck__`, and restoration after mutation. A diagnostic loop of one million positive tuple checks fell from a 0.628 s median on the preserved Release binary to 0.542 s on the candidate (about 1.16× for that microcase; CPython 3.14.7 measured 0.043 s). This is a targeted diagnostic, not an official pyperformance ratio.

Official pyperformance fast-mode SciMark measurements against the same-day preserved baseline were:

| Case | CPython 3.14.7 | Fixed XLang3 baseline | Candidate | Candidate vs baseline | CPython / candidate |
|---|---:|---:|---:|---:|---:|
| `scimark_lu` | 75.29 ms | 1.11 s | 1.00 s | 1.11× faster | 0.075× |
| `scimark_sor` | 97.53 ms | 854 ms | 827 ms | 1.03× faster | 0.118× |

`pyperf compare_to` found these two cases significantly faster and reported a 1.03× geometric mean across them. It hid `scimark_fft`, `scimark_monte_carlo`, and `scimark_sparse_mat_mult` as not significant; their observed changes are not wins. The official run still marks some results unstable, so repeat before treating the small SOR change as robust. LU's 11% improvement is the clear result. XLang3 remains about 13.3× slower than CPython on LU and 8.5× slower on SOR, so this is progress toward the full goal, not completion.

Raw candidate data and the paired comparison are [`scimark-isinstance-tuple-call-cache-candidate-fast-20261011.json`](data/scimark-isinstance-tuple-call-cache-candidate-fast-20261011.json) and [`scimark-isinstance-tuple-call-cache-paired-compare-20261011.txt`](data/scimark-isinstance-tuple-call-cache-paired-compare-20261011.txt). The control is [`scimark-isinstance-fastpath-baseline-fast-20261011.json`](data/scimark-isinstance-fastpath-baseline-fast-20261011.json); the CPython 3.14.7 reference times are listed above from the earlier representative comparison.

## Current fixed Release SciMark rerun — 2026-10-11

After rebuilding the fixed Release directory from the restored source, the
official `scimark` group completed all five subtests. Against CPython 3.14.7,
the current XLang3 timings are 4.36× slower on FFT, 13.37× on LU, 10.42× on
Monte Carlo, 8.64× on SOR, and 5.55× on sparse matrix multiplication. Their
geometric mean is **7.81× slower**. The five timings are directional fast-mode
results and pyperf marked them unstable.

Compared with the earlier same-day pre-`isinstance` baseline, LU is 1.10×
faster (1.11 s to 1.01 s); `pyperf compare_to` accepts this LU result as
significant. SOR changes by only 1.01×, and FFT, Monte Carlo, and sparse
matrix multiplication do not show a significant difference. The measured
`isinstance(index, tuple)` specialization is therefore a real targeted LU
gain, but it leaves LU 13.37× slower than CPython and does not fix the broad
interpreter gap.

Current five-case JSON: [`scimark-current-fixed-release-full-fast-20261011.pyperf.json`](data/scimark-current-fixed-release-full-fast-20261011.pyperf.json), with [provenance](data/scimark-current-fixed-release-full-fast-20261011.pyperf.provenance.json).

## Rejected unused CallMethod eligibility check — 2026-10-11

Removing an apparently unused `inline_calls_allowed(runtime)` calculation
from the `CallMethod` handler did not improve the paired `richards` and
`typing_runtime_protocols` timings: the observed geometric mean was 1.03×
slower, with unstable samples. The source edit was restored. See the
[trial record](callmethod-unused-inline-eligibility-trial-20261011.md) and
its before/after pyperf JSON files. This confirms the next optimization should
target executed work with strong profile attribution, rather than source-level
dead-looking work that the compiler may already remove.

## Rejected array direct-fastcall trial — 2026-10-11

An array-specific integer-index callback routed through the general VM
`GetItem` opcode regressed all five official SciMark cases versus the saved
same-day Release control (1.41x slower geometric mean). The callback and its
general-opcode plumbing were removed after the measurement; 55/55 tests had
passed with the trial enabled. The result suggests that widening the shared
opcode path costs more than this isolated native callback saves. See the
[trial record](array-direct-fastcall-trial-20261011.md) and its candidate
[pyperf data](data/scimark-array-direct-fastcall-candidate-20261011.pyperf.json).
After removing the trial and rebuilding the fixed Release directory, all
55 CTest checks passed. A restored-source SciMark run landed back in baseline
range: four cases were statistically unchanged and LU was 1.02x slower
([0m1.01x slower geometric mean). Its [raw data](data/scimark-restored-source-20261011.pyperf.json)
and [provenance](data/scimark-restored-source-20261011.pyperf.provenance.json)
are preserved.

## Rejected `_lru_cache_wrapper` positional fast adapter — 2026-10-11

The native `_functools._lru_cache_wrapper` fast adapter was hypothesized to
remove positional argument-vector work from the hot `typing_runtime_protocols`
path. The official case measured 2.75 ms on control and 3.02 ms on candidate
(1.10x slower, with noisy samples). The adapter and its added fixture cases
were removed. The full CTest suite passed 55/55 after restoration. See the
[trial record](lru-cache-positional-fastcall-trial-20261011.md), the [control
data](data/typing-runtime-protocols-lru-fastpath-control-20261011.json), and
the [candidate data](data/typing-runtime-protocols-lru-fastpath-candidate-20261011.json).
The restored-source rerun measured 2.91 ms ± 0.21 ms, which is too noisy to
claim a retained gain or regression; its [data](data/typing-runtime-protocols-lru-fastpath-restored-20261011.json)
and [provenance](data/typing-runtime-protocols-lru-fastpath-restored-20261011.provenance.json)
are retained.

## Rejected string-only `dict.get` index trial — 2026-10-11

A guarded string-index path in runtime `dict.get` reduced the diagnostic
20,000-call primitive from 20.72 ms to 20.01 ms, but `typing_runtime_protocols`
did not improve: 2.75 ms control versus 2.79 ms candidate, hidden by
`pyperf compare_to` as insignificant. The code and fixture changes were
removed. This suggests the dictionary lookup is not the dominant cost once
Python-level call and attribute overhead is included. See the
[trial record](dict-get-string-index-trial-20261011.md), its [primitive
data](data/inspect-static-primitives-dict-string-fastpath-20261011.json), and
[pyperf candidate](data/typing-runtime-protocols-dict-string-fastpath-candidate-20261011.json).

## Rejected direct `array('d')` read intrinsic — 2026-10-11

A narrowly guarded direct read at cached `GetItem` was intended to remove the
native call wrapper for SciMark's exact integer `array('d')` reads. It passed
the 55-check CTest suite but had no measurable overall SciMark gain: FFT was
1.06x faster and Monte Carlo 1.02x faster, the other three cases were
statistically unchanged, and the geometric mean was 1.00x slower. Candidate
samples showed substantial jitter. The code was removed; the native generic
call remains. See the [trial record](array-f64-getitem-inline-trial-20261011.md)
and retained [candidate data](data/scimark-array-f64-getitem-inline-candidate-20261011.pyperf.json).

## Single-star `CALL_FUNCTION_EX` varargs binding — 2026-10-11

The VM binder's existing direct path handled `*args, keyword-only=default`
functions but excluded the common `def f(*args)` signature. Extending the
guard to varargs-only functions lets the binder construct the final fresh
tuple once, instead of materializing expanded arguments and then packing them
again. The fast path is guarded to one exact tuple/list star argument, no
positional or keyword arguments, stable defaults, and an unchanged function
signature; subclasses, iterators, multiple stars, keywords, and dynamic
defaults keep the generic path. The source comment documents the
`asyncio.gather(*coroutines)` motivation and semantic boundary.

An alternating, same-fixed-executable-path A/B used preserved parent and
candidate Release snapshots and ran the same 50,000-call diagnostic five
times per block. Parent blocks had medians 41.2 ms and 41.8 ms; candidate
blocks had medians 22.5 ms and 22.3 ms. The combined medians indicate about
1.85x speedup for this narrow call shape. This is VM argument binding work,
not an IR-generation or dispatch-count change. The fixture
`call_ex_single_star_varargs_binding` matches CPython 3.14.7, and the
interpreter test executable passes.

The result does not translate into a demonstrated broad win: the official
`async_tree_none` measurements around this change were noisy and statistically
unchanged, and the latest full matched pyperformance aggregate remains about
0.141x (XLang3/CPython, or 7.1x slower). The paired raw output is
[`call-ex-single-star-varargs-paired-20261011.txt`](data/call-ex-single-star-varargs-paired-20261011.txt).

## Rejected empty-storage `CallMethod` shadow-scan bypass — 2026-10-11

A warmed, inlined slotted getter still measured 33.2 ms per 100,000 calls.
I tried bypassing the receiver shadow scan only when the instance had no
dynamic attributes or mapping storage, no matching slot, and no custom/native
attribute hooks. Ten alternating source-matched Release pairs showed a
3.41% slower median paired ratio, so the change and its temporary fixture were
removed. The result repeats the negative direction of earlier shadow-scan
trials; further work should target the actual method dispatch/call execution
cost instead of adding another storage-shape guard. See the [trial record](callmethod-empty-storage-bypass-trial-20261011.md)
and [raw samples](data/callmethod-empty-storage-bypass-ab-20261011.log).

## Rejected monitoring-eligibility cache for inlined methods — 2026-10-11

Caching the monitoring-generation proof on a warmed `InlineSelfSlotMethod`
site also failed to help its representative getter loop: the median paired
ratio was 1.0137x slower across ten alternating Release pairs. The helper and
call-site edit were removed. This isolates another small per-call check that
does not explain the method-call gap by itself. See the [trial record](callmethod-monitor-eligibility-cache-trial-20261011.md)
and [raw samples](data/callmethod-monitor-eligibility-cache-ab-20261011.log).

## Rejected early `CallLocalMethod` slot-getter dispatch — 2026-10-11

An early return in the actual `CallLocalMethod` wrapper was intended to skip
the receiver-register copy and general `CallMethod` dispatcher for a warmed
slotted getter. The same-site override/delete fixture passed on both runtimes,
but ten alternating Release pairs still measured 1.0143x slower at the median.
The fast path and temporary fixture were removed. This rules out this early
dispatch form; the measured method-call gap needs a larger VM execution change.
See the [trial record](calllocalmethod-entry-fastpath-trial-20261011.md) and
[raw samples](data/calllocalmethod-entry-fastpath-ab-20261011.log).

## Exact-tuple `_contextvars.Context.run` expansion — 2026-10-12

The generic native-call adapter copied exact tuple arguments for the common
`asyncio.Handle._run` `Context.run(callback, *args)` shape. An opt-in profile
counted 65,322 expanded calls. A guarded fast callback now lends the immutable
tuple storage directly to the callback invocation, while mutable lists,
subclasses, arbitrary iterables, and keywords retain general expansion. Five
alternating 100,000-call microbenchmark pairs measured a 1.25x median gain
over the saved r5 Release and still 7.6x slower than CPython 3.14.7. The
official `async_tree` result was statistically insignificant (3.90 s ± 0.28 s
candidate versus 3.87 s ± 0.12 s baseline), so this is a narrow adapter gain,
not a demonstrated broad async-tree improvement. See the
[trial record](context-run-tuple-star-fastpath-trial-20261012.md).
