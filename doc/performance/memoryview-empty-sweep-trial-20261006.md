# Empty memoryview sweep gate trial (2026-10-06)

## Result

Rejected. Gating `release_memoryviews_last_used_at()` at the opcode call site
when `memoryview_registers` was empty did not produce a significant pyperformance
improvement. The candidate stayed close to control on both interpreter-heavy
cases, with a 0.7% higher `subparsers` mean and a 0.5% lower SQLGlot parse
mean. `pyperf compare_to` hid both merged comparisons as not significant. The
call-site edit was removed and the fixed Release executable and runtime DLL
were restored.

| Benchmark | CPython 3.14.7 reference | Control mean | Candidate mean | Result |
| --- | ---: | ---: | ---: | --- |
| `subparsers` | 8.151 ms | 147 ± 10 ms | 148 ± 10 ms | Not significant |
| `sqlglot_v2_parse` | 1.010 ms | 21.5 ± 1.5 ms | 21.4 ± 1.5 ms | Not significant |

The control and candidate medians were 145 and 146 ms for `subparsers`, and
21.1 ms for SQLGlot parse in both. The current evidence still puts XLang3 at
about **0.055× CPython speed** on `subparsers` and **0.047×** on SQLGlot parse;
this trial did not close those gaps.

## Method and validation

Both runs used pyperformance 1.14.0 in rigorous mode and the unchanged official
benchmark bodies. The manager was `C:\Python\Python314\python.exe` (Python
3.14.7); XLang3 received the repository's existing benchmark dependency site,
whose interpreter is also Python 3.14.7. Each merged result contains 240 values
per benchmark across 80 measured runs. Individual pyperf runs still warned
about high outliers and instability, so only the absence of a significant
change is claimed.

The candidate passed the `binary_buffers`, `memoryview_iteration`,
`bytesio_export_lifetime`, and `pickle_module` fixtures, plus
`xlang3_interpreter_tests.exe` and `xlang3_runtime_value_tests.exe`. The
original Release pair was restored at
`build-repro/main-verify-20261006/Release` and hash-checked:

| Build | Executable SHA-256 | Runtime DLL SHA-256 |
| --- | --- | --- |
| Fixed control | `FF66E309BED7F3226F52F59E06842F448992F31805D54685EA755365CCD2D389` | `395BF96C94508A8E9E326D2C79C427489B729AC84CD244BACD4EDC037F331B1E` |
| Rejected candidate | `D8D151806BE42BA0EA61FD67A404DD130C3A3CEA6E08550854E4E13FDBAE8FBA` | `FFDDFE7612772C4C1ED1F5EE6DCAFE46E81713AD40350145E407381DB3B392AA` |

Raw runs and merged pyperf files:

- [Control run 1](data/pyperformance-memoryview-gate-control-r1-rigorous-20261006.json)
- [Control run 2](data/pyperformance-memoryview-gate-control-r2-rigorous-20261006.json)
- [Candidate run 1](data/pyperformance-memoryview-gate-candidate-r1-rigorous-20261006.json)
- [Candidate run 2](data/pyperformance-memoryview-gate-candidate-r2-rigorous-20261006.json)
- [Merged control](data/pyperformance-memoryview-gate-control-merged-rigorous-20261006.json)
- [Merged candidate](data/pyperformance-memoryview-gate-candidate-merged-rigorous-20261006.json)

This test rules out only the empty memoryview-sweep call as a useful lever.
The VM loop, warmed call path, and register-access costs remain larger targets;
future changes still need an end-to-end result before performance comments or
fast paths are retained.
