# Decimal context dictionary read trial (2026-10-03)

## Result

Rejected. Reading `prec`, `Emin`, `Emax`, and `clamp` directly from an exact,
unchanged `_pydecimal.Context` instance dictionary did not improve the
official pyperformance 1.14.0 `telco` benchmark. The candidate's two
`--fast` runs were **226 ms ± 24 ms** and **229 ms ± 27 ms**. The controls
measured **206 ms ± 8 ms** and **259 ms ± 116 ms**. `pyperf compare_to` found
both candidates 10–11% slower than the first control; the second control was
too noisy to establish a reliable overall difference. There is no measured
gain to justify retaining the extra path.

The implementation guarded direct dictionary reads by exact Context class
identity and class version, preserving ordinary attribute lookup for
subclasses and modified classes. Decimal arithmetic, quantize, and string
fixtures passed, as did the runtime-value and interpreter C++ tests. The
optimization was removed after the benchmark failed to show a benefit.

## Measurement details

All four official `telco --fast` runs used CPython **3.14.7**, pyperformance
1.14.0, and the fixed executable path
`D:\CantorAI\xlang3\build-repro\Release\xlang3.exe`. The unchanged control
binary hashes were `B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA`
(exe) and `330BA0B48A931AEF5B927DD0151C0ADF062A9FC5A0C4BC345C51F2BF957DF23F`
(runtime DLL). The rejected candidate hashes were
`0AFEC556519BCC1DB1F91265E09CDD5CA59CD4753D1723056BD961465BEB6B8D` (exe)
and `4E955FFDF4988DA11E583E28F0CED2755137AA583D7E4DDDCAF34DEF6CD5E686`
(runtime DLL). The benchmark compares with XLang3's saved CPython 3.14.7
`telco` result of **5.755 ms**; these candidate runs remain roughly **39×**
slower.

Raw data:

- [Control run 1](data/decimal-context-dict-fast-control-20261003.json)
- [Control run 2](data/decimal-context-dict-fast-control-r2-20261003.json)
- [Candidate run 1](data/decimal-context-dict-fast-candidate-20261003.json)
- [Candidate run 2](data/decimal-context-dict-fast-candidate-r2-20261003.json)

The fixed executable path was restored to the control hashes after the test.
