# `telco`: exact Decimal.Context attribute fast-path trial (2026-10-02)

**Status: rejected; no repeatable performance gain.** The candidate bypassed
generic attribute/descriptor resolution when the current context was an exact,
unchanged `_pydecimal.Context`, reading `prec`, `Emin`, `Emax`, and `clamp` from
its instance storage. Subclasses, custom attribute hooks, modified class
layouts, and materialized `__dict__` values retained their Python-visible
lookup behavior. The focused `decimal_native_arithmetic.py` fixture passed.

The hypothesis was that these four lookups repeated in XLang3's native
`_decimal` add/multiply path were contributing to the remaining `telco` gap.
The trial did not change `_pydecimal` or add a C++ replacement for that
pure-Python module.

## Results

The official pyperformance 1.14 `telco` case ran against the saved Release
control and candidate executables, using the same Python 3.13 standard library,
dependency site, and compatibility shim. Debug samples in seconds were:

| Variant | Samples | Median |
|---|---|---:|
| Control | 0.285, 0.297, 0.344, 0.291 | 0.297 |
| Candidate | 0.326, 0.290, 0.295, 0.272 | 0.295 |

The fast-mode distributions were **295 ms ± 23 ms** for control and **296 ms ±
37 ms** for candidate; both were marked unstable. They overlap completely and
do not support a speedup. The roughly 0.7% difference between debug medians is
below measurement noise, so the implementation was removed.

The broader saved comparison remains **275.8 ms** for XLang3 versus **5.75 ms**
for CPython 3.14.7 (about **47.9× slower**). This trial did not materially
reduce that gap.

## Evidence and restoration

- Debug JSON: [control](data/telco-context-attr-control-debug-20261002.json),
  [candidate](data/telco-context-attr-candidate-debug-20261002.json), and
  [paired control/candidate runs](data/telco-context-attr-control-debug-r1-20261002.json),
  [candidate r1](data/telco-context-attr-candidate-debug-r1-20261002.json),
  [control r2](data/telco-context-attr-control-debug-r2-20261002.json),
  [candidate r2](data/telco-context-attr-candidate-debug-r2-20261002.json),
  [control r3](data/telco-context-attr-control-debug-r3-20261002.json), and
  [candidate r3](data/telco-context-attr-candidate-debug-r3-20261002.json).
- Fast distributions: [control](data/telco-context-attr-control-fast-20261002.json)
  and [candidate](data/telco-context-attr-candidate-fast-20261002.json).
- The Release executable and runtime DLL were restored from the preserved
  pre-trial copies after the rollback build completed. SHA-256: executable
  `5F3E4D0F5ED27BFA0DC4F6D19863638CFCDA0B5275BC06F1A825DE7D521FD79C`, runtime
  DLL `3A67FE039F835AA5C2714596253D9FEFFFBEFCA6F70A4B8C7E7926DF84DDB763`.

The next work should target a cost with a larger measured share than these
four context attribute lookups; this path is not retained in the runtime.
