# `telco`: XLang3 Decimal performance gap (2026-10-02)

`telco` remains a large pyperformance gap. The saved matched full-suite run
measured CPython 3.14.7 at 5.75 ms and XLang3 at 3.42 s. After the zero-
operand optimization, two focused official `pyperformance --fast` runs
measured XLang3 at **286 ms** and **282 ms**. Against the saved CPython 3.14.7
focused reference at **5.98 ms**, those are **47.89×** and **47.17×** slower.
The two XLang3 runs are **1.66×** and **1.69×** faster than the immediately
preceding 476 ms native-Decimal candidate, but they remain far from CPython.

The later 97-definition XLang3 full run measured `telco` at **275.8 ms**,
matching the focused result within its fast-mode variation. Against the saved
full-suite CPython result of **5.75 ms**, XLang3 remains **47.9× slower**. A
fresh direct profile of the current Release candidate still counted **32,512**
native binary fast paths and **12,507** native quantize fast paths, with only
**9** binary fallbacks and no quantize fallbacks. This confirms the large
remaining cost is in the native path plus XLang3's surrounding dispatch, not
fallback arithmetic. The profile is in
[`telco-decimal-pickle-full-fast-profile-20261002.txt`](data/telco-decimal-pickle-full-fast-profile-20261002.txt).

XLang3 registers its own native `_decimal` module, the same module boundary
used by CPython. It leaves `decimal.py` and `_pydecimal.py` as Python and uses
the latter as a compatibility fallback. Finite addition, multiplication,
quantize, and Decimal formatting already have guarded native paths. The
project rule against replacing pure-Python standard-library code with a C++
implementation is preserved.

The latest profile of the unchanged pyperformance `bm_telco` body found that
`decimal_parts()` rejected a finite Decimal whose coefficient was zero. Those
zero operands are routine in telco's running totals, so thousands of ordinary
additions fell back to `_pydecimal`. The native path now accepts zero
coefficients, preserves the sign for same-sign zero addition and zero
multiplication, and keeps opposite-sign exact cancellation on the Python path
because its zero sign depends on `Context.rounding`.

On alternating direct runs of the pyperformance workload body, the
zero-operand candidate's median was **252.622 ms**, compared with **466.406 ms**
for the original Release control, a **1.85×** speedup. The body-only CPython
3.13 median was 5.578 ms, so the candidate remains about **45.3× slower** in
that diagnostic. Native profiling changed from 3,618 binary fallbacks and 64
quantize fallbacks to 9 binary fallbacks and no quantize fallbacks. The raw
samples and limitations are in
[`data/telco-decimal-zero-fastpath-direct-20261002.txt`](data/telco-decimal-zero-fastpath-direct-20261002.txt).

The focused candidate scores are official pyperformance 1.14.0 `--fast`
results. Raw files are
[`candidate run 1`](data/telco-decimal-zero-fast-release-fast-20261002.json),
[`candidate run 2`](data/telco-decimal-zero-fast-release-fast-r2-20261002.json),
and the [`CPython 3.14.7 reference`](data/telco-decimal-cpython-fast-20261002.json).
`pyperf compare_to` reports the ratios above; the immediately preceding native
candidate is preserved in
[`telco-decimal-native-profile-fast-20261002.json`](data/telco-decimal-native-profile-fast-20261002.json).
The XLang3 executable SHA-256 is
`FF789588391A20E980C5F5E31AF776A946B213E1DBEBD2E4524CB449582B80E8`; its
runtime DLL SHA-256 is
`594BA4883C7A3EEF455F1DBBC5DB9E440F85948B6413B91FA931B90CA28D5CC3`.

There is a standard-library mismatch in this comparison: the configured
CPython 3.14 standard-library directory returns Access Denied, so XLang3 loads
the accessible CPython 3.13 standard library while the reference uses CPython
3.14.7's library. The pyperformance definition and dependency site are shared,
but this is not a fully matched runtime-and-stdlib comparison. The direct body
diagnostic likewise uses CPython 3.13 and remains separate from the official
3.14 reference. The full-suite objective remains open.

The durable fast-path rationale and signed-zero regression cases are in
[`decimal_module.cpp`](../../src/runtime/modules/system/decimal_module.cpp)
and [`decimal_native_arithmetic.py`](../../tests/fixtures/core/decimal_native_arithmetic.py).

## Follow-up stage profile

A direct run of the unchanged one-loop `bm_telco` body with
`XLANG3_DECIMAL_PROFILE_TIMING=1` counted 32,512 native binary fast paths and
12,507 native quantize fast paths, with nine binary fallbacks. The instrumented
Decimal fast-path stages accumulated 3.9 ms in operand extraction, 22.6 ms in
context checks, 5.9 ms in arithmetic, 1.0 ms in rounding, and 18.8 ms in result
construction. The full instrumented body took 616 ms, versus about 290 ms in
the earlier uninstrumented body profile, so instrumentation substantially
changes runtime; these stage totals identify where to investigate but are not
a clean time breakdown of the uninstrumented 275 ms benchmark. Raw output:
[`telco-decimal-stage-timing-diagnostic-20261002.txt`](data/telco-decimal-stage-timing-diagnostic-20261002.txt).

A separate pyperformance `--fast` run with the profile environment enabled
measured 305 ms ± 36 ms. Because pyperformance runs the benchmark in worker
processes, the profile counters printed by the controller were zero; do not
use that run for Decimal stage attribution. Its JSON remains at
[`telco-decimal-stage-timing-fast-20261002.json`](data/telco-decimal-stage-timing-fast-20261002.json).
