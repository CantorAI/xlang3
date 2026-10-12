# Release Value refcount-counter gate screen (2026-10-06)

## Result

Rejected. The candidate compiled the optional `Value` incref/decref diagnostic
flag check out of optimized Release builds. It kept the atomic reference-count
operations themselves unchanged. The fast pyperformance screen showed no
improvement in the copy-heavy pickle or SQLGlot cases and a 2% slowdown in
`subparsers`; the candidate should not be retained.

| Benchmark | CPython 3.14.7 reference | Control | Candidate | `pyperf compare_to` |
| --- | ---: | ---: | ---: | --- |
| `subparsers` | 8.151 ms | 146 ± 2 ms | 148 ± 2 ms | Candidate 1.02× slower |
| `pickle_pure_python` | 273.5 µs | 5.18 ± 0.07 ms | 5.18 ± 0.06 ms | Not significant |
| `sqlglot_v2_parse` | 1.010 ms | 21.6 ± 1.2 ms | 21.7 ± 1.3 ms | Not significant |

The comparisons leave XLang3 at roughly 0.056×, 0.053×, and 0.047× CPython
speed on those cases, respectively. A one-run fast screen is not a final
performance estimate; it is sufficient to reject a candidate that has no
directional gain and regresses its other measured case.

## Method and validation

The official pyperformance 1.14.0 benchmark bodies ran through the repository's
Windows-compatible harness. `C:\Python\Python314\python.exe` (3.14.7) was the
manager, and the existing benchmark dependency site was also verified as
Python 3.14.7. The same benchmark body, harness, and dependency site were used
for both builds; only the tested source guard and Release binaries differed.
The fixed executable path did not change.

The candidate passed the full `tests/run_fixtures.py` suite,
`xlang3_interpreter_tests.exe`, and `xlang3_runtime_value_tests.exe`. The source
edit was removed after comparison, and the original binary pair was restored
at `build-repro/main-verify-20261006/Release`:

| Build | Executable SHA-256 | Runtime DLL SHA-256 |
| --- | --- | --- |
| Fixed control | `FF66E309BED7F3226F52F59E06842F448992F31805D54685EA755365CCD2D389` | `395BF96C94508A8E9E326D2C79C427489B729AC84CD244BACD4EDC037F331B1E` |
| Rejected candidate | `E22B6FCE0F04C8A8AFFA7EFD15607B64FC38870EA6B584940993A97A0B0A2E36` | `1B78882BDB269086D3DC615205247AA0D3FCDC5E6E413234175B64A1FB10316B` |

The source hypothesis was that a global relaxed atomic diagnostic-flag load on
every `Value` retain/release was a material cost. Removing it did not improve
these workloads. The hot cost appears to lie deeper in value ownership and
shared interpreter execution; future work should target those paths without
repeating this flag-gating experiment.

Raw results: [control](data/pyperformance-valuecounter-control-r1-fast-20261006.json),
[candidate](data/pyperformance-valuecounter-candidate-r1-fast-20261006.json).
