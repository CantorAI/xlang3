# Trace disabling and Coverage checkpoint

The VM now stops all trace events when `sys.settrace(None)` is called, including
events for frames that retain a local trace hook. The local hooks remain intact
so tracing can resume when enabled again. This fixes a reproduced Coverage
stack-underflow failure. The original official Coverage benchmark now completes;
it remains substantially slower than CPython 3.14.7.

The preceding runtime repair propagates a nested callback's trace-setting change
to saved frame states. It publishes every replacement before retiring old hook
owners, so finalizer reentry cannot restore a stale setting. Event dispatch owns
its selected callback through materialization and completion. C++ audits cover
last-owner self-disable, replacement, and cleanup reentry across saved states.
Both comments and regression fixtures are included. No pure-Python library body,
IR format, instruction shape, or benchmark workload was replaced.

## Cause and validation

The two-group regression records only `outer_off:call` and `disable:call` on
CPython after the callback disables tracing. The previous X candidate also
emitted two local line events and `outer_off:return`. In an instrumented Python
Coverage tracer, those extra returns popped `data_stack` after tracing stopped,
eventually raising `IndexError: pop from empty list`. The VM callback failure
left no valid result exception for the CLI formatter, obscuring the original
error with its `print_exception` complaint.

The new candidate matches both CPython groups, including reactivation. Fresh
validation passed **405 core fixtures, 11 compatibility sections, three expected
failures, nine selected CTests, and both SQLite API checks**. The complete default
11-case fixed gate passed with **21 repeats, five warmups and the unchanged 10%
threshold**. The first complete gate was inconclusive for `json_dumps`: its first
interval crossed 1.10 while its confirmation passed. Both attempts are preserved;
one fresh complete gate then passed. No thresholds, baseline, cases, or loops
were changed. No further gate retry was needed.

The guard lives inside event emission. It adds no new check to ordinary
unmonitored opcode execution. The sticky trace-capability hint is not current
thread enablement. Existing local hook ownership is preserved during suspension.

## Original Coverage result

| Original official case | Saved CPython 3.14.7 mean | Current XLang3 mean | X speed, CP = 1× | X elapsed, CP = 1× |
|---|---:|---:|---:|---:|
| `coverage` | 0.065513040 s | 5.627007585 s | 0.011643× | 85.891× |

All 20 values from each runtime are retained. CPython is the saved October 7
full run; XLang3 is the fresh affected-case run. These fast-mode measurements
are unpaired, not evidence of statistical significance. X standard deviation
is 0.221233331 s; raw warnings remain in the log. The trace-changed
warning and exception-formatting error are absent from the completed run.

A separate **current, untimed** backend probe used the same Coverage 7.3.2
package and no core override: CPython selected native `coverage.CTracer` from
its CPython extension; XLang3 selected `coverage.pytracer.PyTracer`. This is a
real current native-backend gap. It is not independent proof of the saved
October 7 worker's backend. Any native replacement must be XLang3's own native
module with the compatible import/API; CPython's extension is not reused.

The previously published full97 matrix remains the older **75 completed / 22
failed** capture. This checkpoint retested Coverage only; it does not relabel
that full run as 76/97 or recompute its aggregate from a mixed capture.

## Remaining call-path evidence

The balanced artificial diagnostic compares a Python wrapper calling
`object.__new__(cls)` with another calling a saved native callable. Both retain
ordinary Python function entry, the same class and allocation, and the same
10000-operation loop. All six runtime permutations and mirrored case orders
produced 18 serial children and 36 timed loops; every value remains available.

| Artificial route, µs/call | CPython 3.14.7 | Accepted X R5 control | Current X |
|---|---:|---:|---:|
| `original_lookup` | 0.076370 | 0.931180 | 0.858995 |
| `saved_native_lookup` | 0.075160 | 0.545090 | 0.528535 |

The altered saved-call policy intentionally does not preserve dynamic attribute
replacement. It is diagnostic, not an accepted engine optimization. Its
difference includes lookup, dispatch and different global/register loads;
it is not a pure lookup CPU share, pickle workload fraction or predicted suite
gain. The source135 IR was retained for the identical child and unchanged
recorded compiler inputs; fresh source137/control semantics were checked without
timing. No fresh IR emission or dynamic optimization-hit count is claimed.

## Evidence and scope

[Current full correctness](data/trace-disable-local-events-correctness-20261009.json),
[fixed gate and original Coverage](data/trace-disable-local-events-performance-r2-20261009.json),
[first inconclusive gate](data/trace-disable-local-events-performance-20261009.json),
[all 40 Coverage values](data/trace-disable-local-events-checkpoint-20261009-coverage-values.csv),
[all 36 route loops](data/trace-disable-local-events-checkpoint-20261009-route-values.csv),
[balanced route receipt](data/object-new-route-current-balanced-20261009.json),
[current backend observation](data/coverage-backend-untimed-20261009.json).

The measured build is the exact recorded source137 worktree and Release178
receipt at `build-repro/main-verify-20261006/Release/xlang3.exe`; the fixed
baseline177 remains unchanged. Only ten owned engine/test paths are staged.
Unrelated dirty files are preserved and excluded. The selected source inventory
and exact owned-source archive are partial provenance, not a clean-checkout
reproduction claim. Runtime/control binaries are not checked in. Engine index
line endings are normalized to LF; measured owned bytes are archived exactly.

Strict idle checks preceded/followed timing. The one-second compiler/CTest
watcher observed no overlap or scanner failure; processes between samples may
be missed. Official execution pins cover the recorded sources, binaries,
runner and gate cases; this receipt does not retrospectively prove every
transitive benchmark/dependency/native byte. The backend/minimal probes retain
their separate scope. There is no broad CPython speed win or whole-goal
completion claim.
