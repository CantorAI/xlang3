# Empty inline-cache teardown fast path (2026-10-06)

## Decision

Rejected. The proposed fast return for `XlangVMCacheDomain::Empty` did not
produce a significant improvement against the current `main` build. It has
been removed from the source.

## Why the first result was misleading

Seven alternating direct-body samples and two focused pyperformance fast runs
appeared to show a 9–12% improvement in the three `deepcopy` cases. Those
control runs used the fixed Release executable, however, which predates the
current `main` scratch build. They were not a matched control for this
candidate. The raw focused files are preserved for audit:

- [Control A](data/pyperformance-deepcopy-cache-empty-control-a-20261006.json)
- [Candidate A](data/pyperformance-deepcopy-cache-empty-candidate-a-20261006.json)
- [Candidate B](data/pyperformance-deepcopy-cache-empty-candidate-b-20261006.json)
- [Control B](data/pyperformance-deepcopy-cache-empty-control-b-20261006.json)

The valid comparison used the saved full pyperformance run from the exact
current-main scratch build before the edit, then a full candidate run. On the
51 matched subtests, `pyperf compare_to` found no statistically significant
overall change and rounded the geometric mean to **1.00×**. A direct mean
comparison gives only **1.002×** candidate/current-main. Against CPython
3.14.7, the aggregate moved from **0.18251×** to **0.18294×**; five cases were
faster in both runs. This is noise-level movement and does not support
retaining the branch.

| Case | Current-main full run | Candidate full run | Candidate/current-main |
|---|---:|---:|---:|
| `deepcopy` | 2.451 ms | 2.448 ms | 1.001× |
| `deepcopy_reduce` | 27.32 μs | 27.04 μs | 1.010× |
| `deepcopy_memo` | 264.6 μs | 260.0 μs | 1.018× |
| `pickle_pure_python` | 5.110 ms | 5.110 ms | 1.000× |
| `subparsers` | 146.15 ms | 145.60 ms | 1.004× |

The focused comparison initially used a 2.62–2.65 ms fixed-Release `deepcopy`
control and a 2.37–2.38 ms candidate. Comparing either to the current-main
full-run baseline (2.451 ms) exposes the build mismatch: current `main` was
already as fast as the candidate. The full-run comparison resolves the
question, so the cache change was discarded.

## Full-suite evidence

The candidate all-97 run used pyperformance 1.14.0 through the repository's
Windows-compatible shim, with CPython **3.14.7** as the harness and standard
library. All 97 definitions were attempted; **47** completed and **50** failed
or timed out. The failure list and worker traces are retained in the full
[runner log](data/pyperformance-xlang3-frame-empty-cache-cleanup-full-fast-20261006.log),
and the completed measurements are in the [raw pyperf JSON](data/pyperformance-xlang3-frame-empty-cache-cleanup-full-fast-20261006.json).

The exact candidate was built in `build-repro/main-verify-20261006`:

- Executable SHA-256: `4A068BBEDFDE8BEA5F24DD35DA80D1FFCB3F95578B91CE8CBE699B969D456C7C`
- Runtime DLL SHA-256: `5EE06D4BC60CDA9238C73D4F1392153158908B024DABE537C1D7F7C5D96C92F5`
- Full fixtures and `xlang3_interpreter_tests.exe` passed with the candidate.
- The fixed Release executable and runtime DLL were not modified.

The full-run `deepcopy` median was 2.448 ms, effectively unchanged from the
2.451 ms current-main measurement. The full-suite report and chart for the
current-main CPython comparison remain the [latest baseline](pyperformance-xlang3-main-after-exception-guard-full-fast-20261006.md).

## Next step

The frame-pop profile was not sufficient evidence that cold cache cleanup was
a material shared cost. Future frame-cleanup work needs a matched
current-main/candidate benchmark before a favorable focused result is treated
as a speedup. Continue on the separately measured call-dispatch and asyncio
gaps rather than extending this cache cleanup experiment.
