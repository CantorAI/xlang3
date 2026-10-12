# `telco`: short-coefficient Decimal multiplication trial (2026-10-02)

**Result: rejected; the change did not measurably improve the official case.**
The trial added a linear carry-based product for a short Decimal coefficient
times a longer coefficient. This matches `bm_telco`'s repeated multiplication
of a rate such as `0.00894` by an unpacked 64-bit integer. The existing
grade-school multiplication remained the fallback for other sizes.

The correctness fixture now covers positive, negative, and zero products with
the long-by-short shape, alongside its existing large-coefficient case. The
fixture output matches its expected file on the current Release runtime.

## Official pyperformance results

Both runs used pyperformance 1.14.0, the same Python 3.13 standard-library
path, dependency site, and compatibility overlays. The official `telco` case
was run in both `--fast` and `--rigorous` modes. Pyperf warned that both
distributions were unstable. For the rigorous pair:

| Build | Median | Mean ± standard deviation |
|---|---:|---:|
| Control | 254 ms | 258 ± 14 ms |
| Candidate | 253 ms | 256 ± 15 ms |

`pyperf compare_to` hid the result as statistically insignificant. The
candidate therefore does not establish a speedup and its multiplication path
was removed. The benchmark record still identifies the measured executable
path as `build-repro/Release/xlang3.exe`; an executable hash for that temporary
candidate was not captured. The preserved control executable and runtime
library are under `scratch/performance-trials/decimal-small-multiplier/control/`.

## Validation and evidence

- The updated `decimal_native_arithmetic.py` fixture output matches the
  expected output on the rollback Release build.
- The complete fixed 11-case Release baseline gate passes after rollback;
  see [the gate JSON](data/telco-decimal-small-multiplier-rollback-fixed-gate-20261002.json).
- Fast and rigorous raw data: [fast control](data/telco-decimal-small-multiplier-control-fast-20261002.json),
  [fast candidate](data/telco-decimal-small-multiplier-candidate-fast-20261002.json),
  [rigorous control](data/telco-decimal-small-multiplier-control-rigorous-20261002.json),
  and [rigorous candidate](data/telco-decimal-small-multiplier-candidate-rigorous-20261002.json).

The Python 3.14 standard-library path remains inaccessible on this host, so
the focused XLang3 measurement used Python 3.13 compatibility overlays. This
is a same-environment candidate/control comparison, not a new XLang3-versus-
CPython 3.14 comparison. The overall `telco` gap remains unresolved.
