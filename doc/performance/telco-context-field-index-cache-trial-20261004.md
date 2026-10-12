# Decimal context-field index cache — 2026-10-04

The native `_decimal` arithmetic callback was scanning the active Python
`Context` attribute vector for `prec`, `Emin`, `Emax`, and `clamp` on every
operation. The latest instrumented run attributes 22.6 ms to the context stage
across 32,512 native binary operations, although instrumentation inflates the
overall workload time and those stage totals are diagnostic rather than clean
unprofiled timings.

The callback now caches those four vector positions per thread for the active
Context object. Cache hits still verify Context identity, class version, and
the four attribute names, so replacing the Context, mutating its class, or
deleting/reordering fields cannot redirect reads. A materialized `__dict__`
continues to take precedence through ordinary attribute lookup. The cache
stores positions, not values, so normal writes to `Context.prec` and the other
fields are observed on the next operation. This is an XLang3 native `_decimal`
optimization; `decimal.py` and `_pydecimal.py` remain Python implementations.

## Results

| Measurement | Before | Candidate | Change |
|---|---:|---:|---:|
| Official pyperformance 1.14.0 `telco`, `--fast` | 188 ms ± 1 ms | 185 ms ± 2 ms | 1.02× faster, significant (`t=8.07`) |
| Fixed Release paired `telco` body | 3,492 ms | 188.3 ms | candidate/baseline 0.0540× (95% interval 0.0538–0.0542) |

The first row compares against the previous full-suite candidate's `telco`
result; it is a small incremental improvement, not evidence that XLang3 is
close to CPython. The current result is still about **32× slower** than the
saved CPython 3.14.7 result. The second row measures the entire current
candidate against the preserved fixed Release binary and does not isolate the
index cache from other changes accumulated in that candidate.

The entire 11-case fixed Release regression suite passed: the worst ratio was
`range_for` at 1.013×, below the 10% gate. The complete Python fixture suite
also passed under the required `C:\Python\Python314\python.exe`.

With `XLANG3_DECIMAL_PROFILE_TIMING=1`, the instrumented context stage fell
from the previous 22.6 ms to 2.66 ms for the same 32,512 native binary calls
(8.5× less measured stage time). The instrumented whole loop took 250 ms,
compared with 616 ms before the cache, but timing instrumentation affects the
workload substantially; use the official pyperf result above for the actual
speed claim. The updated diagnostic is
[`telco-context-field-index-cache-stage-profile-20261004.txt`](data/telco-context-field-index-cache-stage-profile-20261004.txt).

## Rejected multiplier experiment

A checked `uint64_t` multiply fast path was also tried in the same module. Its
official `telco` result was 192 ms ± 3 ms versus 186 ms ± 2 ms with only the
Context index cache, a significant **4% slowdown** (`pyperf compare_to -v`,
`t=-8.88`). That multiplication change was removed. Keep the existing bounded
digit algorithm unless a replacement wins a matched benchmark and preserves
the arbitrary-precision fallback behavior.

## Evidence and build identity

- Cache-only official result: [`pyperformance-xlang3-telco-context-field-index-cache-final-fast-20261004.json`](data/pyperformance-xlang3-telco-context-field-index-cache-final-fast-20261004.json) and its [runner log](data/pyperformance-xlang3-telco-context-field-index-cache-final-fast-20261004.log).
- Previous full-run result: [`pyperformance-xlang3-comprehension-regex-fix-full-fast-20261004.json`](data/pyperformance-xlang3-comprehension-regex-fix-full-fast-20261004.json).
- Fixed Release `telco` paired samples: [`telco-context-field-index-cache-final-fixed-baseline-20261004.json`](data/telco-context-field-index-cache-final-fixed-baseline-20261004.json).
- All 11 fixed Release regression cases: [`telco-context-field-index-cache-fixed-release-gate-20261004.json`](data/telco-context-field-index-cache-fixed-release-gate-20261004.json).
- Rejected multiply candidate: [`pyperformance-xlang3-telco-context-field-index-and-mul-fast-20261004.json`](data/pyperformance-xlang3-telco-context-field-index-and-mul-fast-20261004.json), [runner log](data/pyperformance-xlang3-telco-context-field-index-and-mul-fast-20261004.log).
- Candidate executable SHA-256: `2AE30FA9A56B11E9BF3298D5A48CA33357C9FF93C097FD29A8B9D485769A1E63`.
- Candidate runtime DLL SHA-256: `FD3BB2E9FFAFC38B501D6FA6A9AD5DE7A5AAB38792EAF86D40835CF4AE70C44D`.
- Exact-candidate all-97 comparison: [`pyperformance-xlang3-context-field-index-cache-vs-cpython314-fast-20261004.md`](../pyperformance-xlang3-context-field-index-cache-vs-cpython314-fast-20261004.md), with [all-definition status](pyperformance-xlang3-context-field-index-cache-vs-cpython314-fast-20261004-all-97-status.csv), [matched subtests](pyperformance-xlang3-context-field-index-cache-vs-cpython314-fast-20261004-subtests.csv), and [horizontal ratio chart](../pyperformance-xlang3-context-field-index-cache-vs-cpython314-fast-20261004.svg).

The overall CPython performance objective remains open; the next work should
target a larger measured cost than this small Context lookup improvement.
