# Exact-tuple `Context.run` fast call — 2026-10-12

## Finding and change

The async-tree diagnostic run recorded 65,322 slow and 55,992 fast native
`_contextvars.Context.run` calls. The native method already had a
register-backed callback for ordinary calls and an exact-empty-star shortcut,
but a non-empty starred tuple was materialized into the reusable native-call
vector before entering the callback. The new guarded path accepts only one
exact tuple star argument, one explicit callback, and no keywords. It passes
the immutable tuple storage directly to `runtime_call_callable`, using the
same Context entry guard as the ordinary path. Lists, tuple subclasses,
custom iterables, multiple expansions, and keyword calls retain the general
expansion path. The implementation comment records why tuple storage is safe
to borrow and why mutable/custom inputs must fall back.

This is native runtime argument plumbing for XLang3's `_contextvars` module,
which corresponds to CPython's native `_contextvars`; it does not replace the
pure-Python `asyncio` or `contextvars` libraries. It changes runtime call
handling, not IR generation.

An opt-in counter profile showed the expanded `Context.run` calls moving from
the generic native callback to the fast callback: the native fast-call count
rose by exactly 65,322, and the `Context.run slow=65322` row disappeared. The
profile-only `CallEx` target-kind counters recorded 65,663 Python functions,
482 native functions, 65,601 classes, and 121,550 bound methods in the same
async-tree body. These counts characterize call shapes; they are not timings.

## Measurements

Five alternating measurements of 100,000 exact-tuple
`Context.run(callback, *args)` calls gave:

| Runtime | Median | Relative result |
| --- | ---: | ---: |
| XLang3 saved r5 Release | 169.5 ms | baseline |
| XLang3 candidate r3 | 135.4 ms | 1.25x faster than baseline |
| CPython 3.14.7 | 17.8 ms | XLang3 candidate remains 7.6x slower |

This focused microbenchmark exercises the expanded call adapter; it is not a
pyperformance score. The measured candidate also contains ongoing source
changes outside this checkpoint, so the timing does not independently attribute
the entire difference to this patch. The script and paired raw values are
[`context_run_tuple_star_micro.py`](../../benchmarks/diagnostics/context_run_tuple_star_micro.py)
and [`the candidate r3 measurement log`](data/context-run-tuple-star-micro-r3-ab-20261012.log).
The CPython reference is retained in [the earlier log](data/context-run-tuple-star-micro-ab-20261012.log).

The official `async_tree` pyperformance fast-mode case did not show a broad
win. The saved baseline measured 3.87 s ± 0.12 s; candidate r3 measured
3.90 s ± 0.28 s. Both runs warned that samples were unstable, and
`pyperf compare_to` hid the case as statistically insignificant. The direct
five-pair body timing for the earlier candidate variant was similarly noisy.
The focused path improvement therefore remains a narrow result; the overall
async-tree slowdown and the 0.14125x full-suite r5 geometric mean are still
open.

## Correctness and regression checks

- `context_run_empty_star`, `context_run_bound_callback`, and the new
  `context_run_tuple_star_exception` fixture passed on XLang3 and match
  CPython 3.14.7. The exception fixture proves the Context state is restored
  after a callback raises.
- A fresh checkpoint check matched CPython 3.14.7 and the expected output for
  all five Context fixtures (ten process invocations), including keywords,
  custom/subclass expansion and same-Context re-entry. Its [receipt](data/context-run-tuple-star-checkpoint-focus-20261012.json)
  records the current fixed executable and runtime DLL identities.
- The full default fixed Release gate passed all 11 cases at 21 paired
  samples and five warmups. Ratios are in
  [`the gate report`](data/context-run-tuple-star-fixed-release-gate-20261012.json).
- Release CTest passed 54/55 tests. The remaining
  `xlang3_cli_visual_studio_debugpy_launch` test timed out waiting for the
  debugpy `initialized` event after its launch process disconnected; the
  aggregate log is
  [`here`](data/context-run-tuple-star-ctest-20261012.log), with the failed
  test name in [this file](data/context-run-tuple-star-ctest-failed-20261012.txt).
- The full fixture runner reaches the existing unrelated
  `int_bytes_small_fastpaths` failure (`True.to_bytes` is missing); its output
  is preserved in
  [`the fixture log`](data/context-run-tuple-star-fixtures-20261012.log).
- Official pyperformance raw results:
  [saved baseline](data/pyperformance-async-tree-context-run-baseline-20261012.json),
  [candidate r3](data/pyperformance-async-tree-context-run-candidate-r3-20261012.json).
- The fixed executable path remains
  `build-repro/main-verify-20261006/Release/xlang3.exe`. The saved baseline
  pair is `build-repro/r5-opcode-profile-baseline/Release`; candidate r3
  executable SHA-256 is
  `5731C010C761F1CD7D784EE6A9671E7AD923F3F2789CC9359F046176CB67D9CE`, and
  its runtime DLL SHA-256 is
  `77E1E92132163D0627C1BDBAF9F17599F7C741E227B37C39BE919A47722075A9`.

The full 97-definition suite has not been rerun on this candidate. This change
is a measured local improvement to one call shape, not evidence that XLang3
has closed the full pyperformance gap.

The source checkpoint contains the Context runtime adapter, the native
expanded-call predicate guard, the exception fixture and its runner registration.
Other ongoing engine changes are excluded from this commit. The binary receipts
describe the tested working build and do not assert that this source subset alone
reproduces every aggregate result.
