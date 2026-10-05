# Polymorphic Python-call cache trial (2026-10-03)

## Result

Rejected. A bounded cache for up to five Python-function targets at one `CALL`
site did not produce a significant change in the official pyperformance
1.14.0 `pickle_pure_python` benchmark. The two candidates measured **5.58 ms
± 0.25 ms** and **5.53 ms ± 0.11 ms**; the same-session control measured
**5.46 ms ± 0.17 ms**. `pyperf compare_to` classified the result as not
significant, so the implementation was removed.

The cache retained strong references for pointer safety and kept its lookup
bounded. A differential probe alternated four Python functions at one call
site, then replaced one dispatch-table target; XLang3 matched CPython 3.14.7.
The runtime-value and interpreter C++ tests also passed. Correctness was not
the issue; the pyperformance evidence did not justify the additional cache
state.

## Why this path was tested

XLang3's diagnostic opcode counts for the unchanged pure-Python pickle
benchmark show frequent `CALL` and `CALL_METHOD` execution, and CPython's
`pickle.py` uses a type-dispatch table whose selected unbound Python function
is called at a shared site. The experiment checked whether the existing
one-target call-site cache repeatedly lost this polymorphic target. The result
shows that this call-site shape is not a sufficiently large bottleneck to
explain the benchmark gap on its own.

The diagnostic counters include instrumentation and are not timing data. They
are retained in
[`pickle-pure-python-perf-counters-20261003.txt`](data/pickle-pure-python-perf-counters-20261003.txt).

## Measurement details

All pyperformance runs used CPython **3.14.7**, pyperformance 1.14.0, and the
fixed runtime path `D:\CantorAI\xlang3\build-repro\Release\xlang3.exe`.
Control hashes: exe `B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA`,
DLL `330BA0B48A931AEF5B927DD0151C0ADF062A9FC5A0C4BC345C51F2BF957DF23F`.
Candidate hashes: exe `013EF72BBB75F841900DF0B19C6B11A26F4A75D7FF7AF3642B9868220B0EBA45`,
DLL `E9902DFC7B28620E2E0FA91F0AE3790A99D7463533F9A775B269F751EF9355BE`.
The saved full-suite XLang3 result was **5.819 ms** versus CPython 3.14.7 at
**273.5 µs**, about **21× slower**.

Raw pyperf files:

- [Control](data/pickle-pure-python-polymorphic-call-fast-control-20261003.json)
- [Candidate run 1](data/pickle-pure-python-polymorphic-call-fast-candidate-20261003.json)
- [Candidate run 2](data/pickle-pure-python-polymorphic-call-fast-candidate-r2-20261003.json)

The fixed executable path was restored to the control hashes after the test.
