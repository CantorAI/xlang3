# Decimal Context field lookup trial (2026-10-03)

## Result

Rejected. Reading `prec`, `Emin`, `Emax`, and `clamp` directly from an exact
`_pydecimal.Context` instance did not improve the Decimal-heavy workload. Both
candidate variants were slower than the source-matched Release control, so no
runtime change from this trial remains in `decimal_module.cpp`.

The first candidate checked the exact Context class and class version, then
scanned its instance attributes once to find the four fields. Across 21
order-balanced pairs of the 5,000-record Telco-shaped workload, the candidate
was **1.0184× slower** (95% paired interval **1.0088–1.0364**): control median
271.060 ms, candidate median 279.552 ms.

The second candidate used the stable field positions established by
`_pydecimal.Context.__init__`, re-reading current values at each operation and
falling back when the exact class, class version, field names, or field types
did not match. It was **1.0349× slower** (95% paired interval
**1.0014–1.0403**): control median 263.000 ms, candidate median 271.275 ms.

Both trials used `benchmarks/diagnostics/measure_case_pair.py`, three warmups,
21 order-balanced repeats, and `benchmarks/cases/telco_decimal_context.py`.
The harness checked that each build produced identical output before accepting
timings. The Decimal arithmetic and quantize regression fixtures passed on
both candidates. The first variant's full raw paired data is in
[`telco-shaped-ab.json`](../../scratch/performance-trials/decimal-context-attribute-scan-20261003/telco-shaped-ab.json);
the second is in
[`telco-shaped-index-ab.json`](../../scratch/performance-trials/decimal-context-attribute-scan-20261003/telco-shaped-index-ab.json).

This evidence rules out these four generic attribute reads as an isolated
optimization opportunity for `telco`. It does not explain the roughly 49×
slowdown against CPython 3.14.7; the next work should target the broader VM
dispatch and allocation costs measured in the Decimal and interpreter
profiles.
