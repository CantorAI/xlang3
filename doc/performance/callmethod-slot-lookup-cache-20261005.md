# CallMethod slot-lookup cache experiment — 2026-10-05

## Result

I tested caching the class's instance-slot index (including a cached negative
lookup) at each warmed `CallMethod` site. The cache was guarded by the class
pointer and its monotonically assigned version. Instance slot values were
still checked on every call, and the existing instance-attribute, descriptor,
and generic lookup paths were unchanged.

The full fixed Release regression gate passed all 11 cases. The largest ratio
was `range_for` at 1.033x the preserved control, within the 10% gate. However,
the official rigorous `sqlglot_v2_parse` run measured **20.8 ± 0.3 ms**, while
the prior XLang3 fast-mode run measured **20.5 ± 0.3 ms**. Those modes do not
form a controlled before/after pair, so they show no demonstrated improvement
and cannot establish a regression either. Against CPython 3.14.7's saved
**1.01 ms** result, this build remains **20.57x slower**.

I am not keeping this engine change: reducing one slot-map hash per method
call did not produce an end-to-end win on the case that motivated the work.
The artifacts preserve the result and gate output without claiming a speedup.

## Reproduction

- Official pyperformance 1.14.0 result:
  [`pyperformance-xlang3-sqlglot-parse-method-slot-cache-rigorous-20261005.json`](data/pyperformance-xlang3-sqlglot-parse-method-slot-cache-rigorous-20261005.json)
- CPython comparison:
  `pyperf compare_to pyperformance-cpython314-clean-release-full-fast-20261002.json pyperformance-xlang3-sqlglot-parse-method-slot-cache-rigorous-20261005.json --table`
- Fixed full Release gate:
  [`release-method-slot-cache-gate-20261005.json`](data/release-method-slot-cache-gate-20261005.json)
- Candidate Release executable SHA-256:
  `18C79D8C50129F3853E91A6FA6C76176C6D56EEC361E6E19202C7EEC5E1798EC`
- Candidate runtime DLL SHA-256:
  `4DCFB71C29C3A39A3A03CF66A20CAD17BF845FCDC1EDC8D80023725025FDB6A8`

The source change is not retained. Follow-up work should measure the time
spent in instance-shadow checks and method dispatch themselves before
extending per-site caches.
